// Draconic::VG::Renderer - :renderer partition.
//
// VGRenderer: draws VGContext/VGBatch content through the RHI. Owns per-frame
// vertex/index/uniform ring buffers (byte-offset sub-allocated across slices),
// the pipeline, and an on-demand ImageData->GPU texture cache. Ported from
// Sedulous.VG.Renderer/VGRenderer.bf.
//
// Deviations from Sedulous (deliberate, documented):
//   * Initialize takes the two pre-compiled rhi::ShaderModule* (vert, frag)
//     rather than a ShaderSystem - shader compilation (DXC) is the caller's job
//     via draconic.shaders, keeping this lib's dependency to pure RHI.
//   * Per-renderer external textures ARE supported (RegisterExternalTexture /
//     UnregisterExternalTexture) so a caller-owned rhi::TextureView - e.g. a
//     ui::viewport offscreen render target - can be sampled via DrawImage. The
//     SHARED cross-renderer external texture cache (VGExternalTextureCache) is
//     still omitted (a multi-renderer/multi-window sharing optimisation); add
//     later if a single RT must be sampled by more than one window's renderer.

module;
#include "Draconic.Core/Prelude.h"

export module draconic.vg.renderer:renderer;

import draconic.core;
import draconic.rhi;
import draconic.image;
import draconic.texture;
import draconic.vg;
import :vertex;

using namespace draconic::core;

export namespace draconic::vg::renderer
{
    namespace rhi = draconic::rhi;
    namespace image = draconic::image;

    /// Projection uniform (one per slice, padded to UniformSlotSize on the GPU). The DF fields
    /// carry the MSDF spread + atlas size for the distance-field fragment shader's screen-space
    /// AA; they are ignored by the default pipeline.
    struct VGUniforms
    {
        Float4x4 projection = Float4x4::Identity();
        f32 dfPxRange = 4.0f;
        f32 dfAtlasW = 512.0f;
        f32 dfAtlasH = 512.0f;
        f32 pad = 0.0f;
    };

    /// A handle to one batch's data inside the shared frame buffers. Returned by
    /// Prepare, consumed by Render. Invalid slices are no-ops.
    struct VGRenderSlice
    {
        u32 vertexByteOffset = 0;
        u32 indexByteOffset = 0;
        u32 uniformByteOffset = 0;
        i32 drawCommandStart = 0;
        i32 drawCommandCount = 0;
        bool isValid = false;
    };

    /// Renders VGBatch content via the RHI (alpha-blended, analytical-AA).
    /// Does not own the device/swapchain.
    class VGRenderer
    {
    public:
        VGRenderer() = default;
        ~VGRenderer() { Dispose(); }
        VGRenderer(const VGRenderer&) = delete;
        VGRenderer& operator=(const VGRenderer&) = delete;

        [[nodiscard]] bool IsInitialized() const { return m_initialized; }

        /// Initialize with a device + the (already compiled) vg vertex/fragment
        /// shader modules + the render-target format + frame count.
        Status Initialize(rhi::Device& device, rhi::ShaderModule& vertShader,
                          rhi::ShaderModule& fragShader, rhi::TextureFormat targetFormat,
                          i32 frameCount, rhi::ShaderModule* dfFragShader = nullptr)
        {
            m_device = &device;
            m_queue = device.GetQueue(rhi::QueueType::Graphics, 0);
            m_targetFormat = targetFormat;
            m_frameCount = frameCount;

            if (!CreateSampler().IsOk())
                return ErrorCode::Unknown;
            if (!CreateLayouts().IsOk())
                return ErrorCode::Unknown;
            if (!CreatePipelineInto(vertShader, fragShader, m_pipeline).IsOk())
                return ErrorCode::Unknown;
            // Optional distance-field pipeline (same layout/vertex format, MSDF fragment shader).
            if (dfFragShader != nullptr &&
                !CreatePipelineInto(vertShader, *dfFragShader, m_dfPipeline).IsOk())
                return ErrorCode::Unknown;
            if (!CreatePerFrameResources().IsOk())
                return ErrorCode::Unknown;

            m_frameVertexOffsets.Resize(static_cast<usize>(frameCount));
            m_frameIndexOffsets.Resize(static_cast<usize>(frameCount));
            m_frameUniformSlotCount.Resize(static_cast<usize>(frameCount));

            m_initialized = true;
            return ErrorCode::Ok;
        }

        /// Reset per-frame ring-offset state. Call once before the frame's first Prepare.
        void BeginFrame(i32 frameIndex)
        {
            m_frameVertexOffsets[static_cast<usize>(frameIndex)] = 0;
            m_frameIndexOffsets[static_cast<usize>(frameIndex)] = 0;
            m_frameUniformSlotCount[static_cast<usize>(frameIndex)] = 0;
            m_drawCommands.Clear();
            m_batchTextures.Clear();
        }

        /// Upload one batch into the shared frame buffers; returns a slice token.
        VGRenderSlice Prepare(draconic::vg::VGBatch& batch, i32 frameIndex, u32 width, u32 height)
        {
            const u32 vertCountIn = static_cast<u32>(batch.vertices.Size());
            const u32 idxCountIn = static_cast<u32>(batch.indices.Size());
            if (vertCountIn == 0 || idxCountIn == 0)
                return VGRenderSlice{};

            const u32 vertByteSize = vertCountIn * static_cast<u32>(sizeof(VGRenderVertex));
            const u32 idxByteSize = idxCountIn * static_cast<u32>(sizeof(u32));
            const u32 sliceVertOffset = m_frameVertexOffsets[static_cast<usize>(frameIndex)];
            const u32 sliceIdxOffset = m_frameIndexOffsets[static_cast<usize>(frameIndex)];
            const u32 sliceUniformSlot = m_frameUniformSlotCount[static_cast<usize>(frameIndex)];

            const u32 maxVertBytes = static_cast<u32>(MaxVertices * sizeof(VGRenderVertex));
            const u32 maxIdxBytes = static_cast<u32>(MaxIndices * sizeof(u32));
            if (sliceVertOffset + vertByteSize > maxVertBytes ||
                sliceIdxOffset + idxByteSize > maxIdxBytes ||
                sliceUniformSlot >= static_cast<u32>(MaxUniformSlots))
                return VGRenderSlice{}; // capacity exceeded

            const u32 sliceUniformOffset = sliceUniformSlot * static_cast<u32>(UniformSlotSize);
            const i32 sliceCmdStart = static_cast<i32>(m_drawCommands.Size());
            const i32 textureBase = static_cast<i32>(m_batchTextures.Size());

            // Convert + upload this slice's vertices.
            Array<VGRenderVertex> renderVerts;
            renderVerts.Reserve(batch.vertices.Size());
            for (usize i = 0; i < batch.vertices.Size(); ++i)
                renderVerts.PushBack(VGRenderVertex(batch.vertices[i]));
            WriteBuffer(m_vertexBuffers[static_cast<usize>(frameIndex)], sliceVertOffset,
                        renderVerts.Data(), vertByteSize);

            // Upload indices verbatim (relative to the slice's vertex base).
            WriteBuffer(m_indexBuffers[static_cast<usize>(frameIndex)], sliceIdxOffset,
                        batch.indices.Data(), idxByteSize);

            // Append textures (commands index into the shared batch-texture list).
            for (usize i = 0; i < batch.textures.Size(); ++i)
                m_batchTextures.PushBack(batch.textures[i]);
            for (usize i = 0; i < batch.commands.Size(); ++i)
            {
                draconic::vg::VGCommand cmd = batch.commands[i];
                if (cmd.textureIndex >= 0)
                    cmd.textureIndex = cmd.textureIndex + textureBase;
                m_drawCommands.PushBack(cmd);
            }

            // Write this slice's projection + distance-field metadata into its uniform slot.
            VGUniforms uniforms;
            uniforms.projection = OrthoOffCenter(static_cast<f32>(width), static_cast<f32>(height));
            uniforms.dfPxRange = batch.dfPxRange;
            uniforms.dfAtlasW = batch.dfAtlasW;
            uniforms.dfAtlasH = batch.dfAtlasH;
            WriteBuffer(m_uniformBuffers[static_cast<usize>(frameIndex)], sliceUniformOffset,
                        &uniforms, sizeof(VGUniforms));

            // Bind groups for any newly-added textures.
            for (i32 texIdx = textureBase; texIdx < static_cast<i32>(m_batchTextures.Size());
                 ++texIdx)
                UpdateTextureBindGroup(texIdx, frameIndex);

            m_frameVertexOffsets[static_cast<usize>(frameIndex)] = sliceVertOffset + vertByteSize;
            m_frameIndexOffsets[static_cast<usize>(frameIndex)] = sliceIdxOffset + idxByteSize;
            m_frameUniformSlotCount[static_cast<usize>(frameIndex)] = sliceUniformSlot + 1;

            VGRenderSlice slice;
            slice.vertexByteOffset = sliceVertOffset;
            slice.indexByteOffset = sliceIdxOffset;
            slice.uniformByteOffset = sliceUniformOffset;
            slice.drawCommandStart = sliceCmdStart;
            slice.drawCommandCount = static_cast<i32>(m_drawCommands.Size()) - sliceCmdStart;
            slice.isValid = true;
            return slice;
        }

        /// A scissor rect in FRAMEBUFFER coordinates (what SetScissor takes).
        struct ScissorRect
        {
            i32 x = 0, y = 0;
            u32 width = 0, height = 0;
        };

        /// Map a command's content-space clip rect into framebuffer coordinates for a
        /// viewport whose origin sits at (viewportX, viewportY) with the given content
        /// extent: clamp to the content box first, then offset. Pure (unit-tested).
        [[nodiscard]] static ScissorRect ComputeScissor(const Rectangle& clipRect, i32 viewportX,
                                                        i32 viewportY, u32 width, u32 height)
        {
            const i32 startX = static_cast<i32>(Ceil(Max(0.0f, clipRect.x)));
            const i32 startY = static_cast<i32>(Ceil(Max(0.0f, clipRect.y)));
            const i32 endX =
                static_cast<i32>(Floor(Min(clipRect.x + clipRect.width, static_cast<f32>(width))));
            const i32 endY = static_cast<i32>(
                Floor(Min(clipRect.y + clipRect.height, static_cast<f32>(height))));
            ScissorRect rect;
            rect.x = viewportX + startX;
            rect.y = viewportY + startY;
            rect.width = static_cast<u32>(Max(0, endX - startX));
            rect.height = static_cast<u32>(Max(0, endY - startY));
            return rect;
        }

        /// Dispatch a slice's draws into the active render pass (full-target viewport).
        void Render(rhi::RenderPassEncoder& renderPass, u32 width, u32 height, i32 frameIndex,
                    const VGRenderSlice& slice)
        {
            Render(renderPass, 0, 0, width, height, frameIndex, slice);
        }

        /// Dispatch a slice's draws into a VIEWPORT SUB-RECT of the active pass's target
        /// (split-screen views): content coordinates (0..width, 0..height) map to the
        /// rect at (viewportX, viewportY); every scissor - including the default - is
        /// clamped to that rect, so content never bleeds into a neighboring view. The
        /// slice must have been Prepared with the SAME width/height (the projection).
        void Render(rhi::RenderPassEncoder& renderPass, i32 viewportX, i32 viewportY, u32 width,
                    u32 height, i32 frameIndex, const VGRenderSlice& slice)
        {
            if (!slice.isValid || slice.drawCommandCount == 0)
                return;

            renderPass.SetViewport(static_cast<f32>(viewportX), static_cast<f32>(viewportY),
                                   static_cast<f32>(width), static_cast<f32>(height), 0.0f, 1.0f);
            renderPass.SetPipeline(m_pipeline);
            renderPass.SetVertexBuffer(0, m_vertexBuffers[static_cast<usize>(frameIndex)],
                                       slice.vertexByteOffset);
            renderPass.SetIndexBuffer(m_indexBuffers[static_cast<usize>(frameIndex)],
                                      rhi::IndexFormat::UInt32, slice.indexByteOffset);

            const u32 dynOffsets[1] = {slice.uniformByteOffset};
            i32 currentTextureIndex = -2; // sentinel forces first SetBindGroup
            auto currentDrawMode = draconic::vg::VGDrawMode::Default;

            const i32 cmdEnd = slice.drawCommandStart + slice.drawCommandCount;
            for (i32 i = slice.drawCommandStart; i < cmdEnd; ++i)
            {
                const draconic::vg::VGCommand& cmd = m_drawCommands[static_cast<usize>(i)];
                if (cmd.indexCount == 0)
                    continue;

                // Switch pipeline on draw-mode change (default sampling vs MSDF decode); falls back
                // to the default pipeline when no DF pipeline was built. A pipeline swap forces a
                // bind-group rebind.
                if (cmd.drawMode != currentDrawMode)
                {
                    rhi::RenderPipeline* pipeline =
                        (cmd.drawMode == draconic::vg::VGDrawMode::DistanceField &&
                         m_dfPipeline != nullptr)
                            ? m_dfPipeline
                            : m_pipeline;
                    renderPass.SetPipeline(pipeline);
                    currentDrawMode = cmd.drawMode;
                    currentTextureIndex = -2;
                }

                if (cmd.textureIndex != currentTextureIndex)
                {
                    if (rhi::BindGroup* bindGroup =
                            GetBindGroupForTexture(cmd.textureIndex, frameIndex))
                        renderPass.SetBindGroup(0, bindGroup, Span<const u32>(dynOffsets, 1));
                    currentTextureIndex = cmd.textureIndex;
                }

                if (cmd.clipMode == draconic::vg::VGClipMode::Scissor &&
                    cmd.clipRect.width > 0.0f && cmd.clipRect.height > 0.0f)
                {
                    const ScissorRect scissor =
                        ComputeScissor(cmd.clipRect, viewportX, viewportY, width, height);
                    renderPass.SetScissor(scissor.x, scissor.y, scissor.width, scissor.height);
                }
                else if (cmd.clipMode == draconic::vg::VGClipMode::Scissor)
                {
                    renderPass.SetScissor(0, 0, 0, 0); // empty clip hides everything
                }
                else
                {
                    renderPass.SetScissor(viewportX, viewportY, width, height);
                }

                renderPass.DrawIndexed(static_cast<u32>(cmd.indexCount), 1,
                                       static_cast<u32>(cmd.startIndex), 0, 0);
            }
        }

        /// Clear all cached GPU textures.
        void ClearTextureCache()
        {
            for (usize i = 0; i < m_textureCache.Size(); ++i)
                DisposeCachedTexture(*m_textureCache[i]);
            m_textureCache.Clear();
        }

        /// Register a caller-owned rhi::TextureView (e.g. a viewport's offscreen
        /// render target) under an ImageData identity key, so DrawImage(key, ...)
        /// samples that GPU texture directly instead of uploading CPU pixels.
        /// The view is NOT owned - the caller must UnregisterExternalTexture before
        /// destroying it (that Unregister is the cache-invalidation signal; the
        /// texture cache is otherwise raw-pointer-keyed with no version guard).
        /// Re-registering an existing key rebinds it to the new view (bind groups
        /// are torn down and rebuilt lazily).
        void RegisterExternalTexture(const image::ImageData* key, rhi::TextureView* view)
        {
            if (key == nullptr || view == nullptr || m_device == nullptr)
                return;

            for (usize i = 0; i < m_textureCache.Size(); ++i)
            {
                if (m_textureCache[i]->source != key)
                    continue;
                CachedTexture& c = *m_textureCache[i];
                for (usize f = 0; f < c.bindGroups.Size(); ++f)
                {
                    if (c.bindGroups[f] != nullptr)
                        m_device->DestroyBindGroup(c.bindGroups[f]);
                    c.bindGroups[f] = nullptr;
                }
                c.view = view;
                c.external = true;
                c.gpuTexture = nullptr;
                return;
            }

            UniquePtr<CachedTexture> cached = MakeUnique<CachedTexture>(DefaultAllocator());
            cached->source = key;
            cached->view = view;
            cached->external = true;
            cached->bindGroups.Resize(
                static_cast<usize>(m_frameCount)); // nullptr-filled, built lazily
            m_textureCache.PushBack(Move(cached));
        }

        /// Drop a previously-registered external texture. Tears down its per-frame
        /// bind groups (but never the caller-owned view/texture). Safe to call for
        /// an unknown key. Call before the underlying view is destroyed.
        void UnregisterExternalTexture(const image::ImageData* key)
        {
            if (key == nullptr)
                return;
            for (usize i = 0; i < m_textureCache.Size(); ++i)
            {
                if (m_textureCache[i]->source != key)
                    continue;
                DisposeCachedTexture(*m_textureCache[i]);
                m_textureCache.RemoveAt(i);
                return;
            }
        }

        /// Whether an external (caller-owned) view is currently registered for key.
        [[nodiscard]] bool IsExternalTextureRegistered(const image::ImageData* key) const
        {
            if (key == nullptr)
                return false;
            for (usize i = 0; i < m_textureCache.Size(); ++i)
                if (m_textureCache[i]->source == key)
                    return m_textureCache[i]->external;
            return false;
        }

        void Dispose()
        {
            if (m_device == nullptr)
                return;

            ClearTextureCache();

            DestroyBuffers(m_uniformBuffers);
            DestroyBuffers(m_indexBuffers);
            DestroyBuffers(m_vertexBuffers);

            if (m_pipeline)
                m_device->DestroyRenderPipeline(m_pipeline);
            if (m_dfPipeline)
                m_device->DestroyRenderPipeline(m_dfPipeline);
            if (m_pipelineLayout)
                m_device->DestroyPipelineLayout(m_pipelineLayout);
            if (m_bindGroupLayout)
                m_device->DestroyBindGroupLayout(m_bindGroupLayout);
            if (m_sampler)
                m_device->DestroySampler(m_sampler);

            m_pipeline = nullptr;
            m_dfPipeline = nullptr;
            m_pipelineLayout = nullptr;
            m_bindGroupLayout = nullptr;
            m_sampler = nullptr;
            m_initialized = false;
            m_device = nullptr;
        }

    private:
        struct CachedTexture
        {
            const image::ImageData* source = nullptr;
            rhi::Texture* gpuTexture = nullptr;
            rhi::TextureView* view = nullptr;
            Array<rhi::BindGroup*> bindGroups; // per frame
            bool external =
                false; // view is caller-owned (e.g. a viewport RT) - never destroyed here
        };

        static constexpr i32 MaxVertices = 131072;
        static constexpr i32 MaxIndices = 131072 * 3;
        static constexpr i32 MaxUniformSlots = 64;
        static constexpr i32 UniformSlotSize =
            256; // dynamic-offset alignment (>= sizeof(VGUniforms)=64)

        static void WriteBuffer(rhi::Buffer* buf, u64 offset, const void* data, usize size)
        {
            if (buf == nullptr || size == 0)
                return;
            if (u8* p = static_cast<u8*>(buf->Map()))
            {
                MemCopy(p + offset, data, size);
                buf->Unmap();
            }
        }

        static Float4x4 OrthoOffCenter(f32 width, f32 height)
        {
            // CreateOrthographicOffCenter(0, width, height, 0, -1, 1) (row-vector).
            Float4x4 m = Float4x4::Identity();
            m.m[0][0] = 2.0f / width;
            m.m[1][1] = -2.0f / height;
            m.m[2][2] = -0.5f;
            m.m[3][0] = -1.0f;
            m.m[3][1] = 1.0f;
            m.m[3][2] = 0.5f;
            return m;
        }

        Status CreateSampler()
        {
            rhi::SamplerDesc desc{};
            return m_device->CreateSampler(desc, m_sampler);
        }

        Status CreateLayouts()
        {
            rhi::BindGroupLayoutEntry entries[3];
            entries[0] = rhi::BindGroupLayoutEntry::UniformBuffer(
                0, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment);
            entries[0].hasDynamicOffset =
                true; // one uniform buffer shared across slices via dynamic offset
            entries[1] = rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment);
            entries[2] = rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment);

            rhi::BindGroupLayoutDesc bglDesc{};
            bglDesc.entries = Span<const rhi::BindGroupLayoutEntry>(entries, 3);
            if (!m_device->CreateBindGroupLayout(bglDesc, m_bindGroupLayout).IsOk())
                return ErrorCode::Unknown;

            rhi::BindGroupLayout* const layouts[1] = {m_bindGroupLayout};
            rhi::PipelineLayoutDesc plDesc{};
            plDesc.bindGroupLayouts = Span<rhi::BindGroupLayout* const>(layouts, 1);
            return m_device->CreatePipelineLayout(plDesc, m_pipelineLayout);
        }

        // Build a VG pipeline (shared layout + vertex format) with the given fragment shader into
        // `outPipeline` - used for both the default and the distance-field variants.
        Status CreatePipelineInto(rhi::ShaderModule& vertShader, rhi::ShaderModule& fragShader,
                                  rhi::RenderPipeline*& outPipeline)
        {
            const rhi::VertexAttribute attributes[4] = {
                {rhi::VertexFormat::Float32x2, 0, 0},  // position
                {rhi::VertexFormat::Float32x2, 8, 1},  // texCoord
                {rhi::VertexFormat::Float32x4, 16, 2}, // color
                {rhi::VertexFormat::Float32, 32, 3},   // coverage
            };
            rhi::VertexBufferLayout vbLayout{};
            vbLayout.stride = static_cast<u32>(sizeof(VGRenderVertex));
            vbLayout.attributes = Span<const rhi::VertexAttribute>(attributes, 4);
            const rhi::VertexBufferLayout vertexBuffers[1] = {vbLayout};

            rhi::ColorTargetState colorTarget{};
            colorTarget.format = m_targetFormat;
            // Premultiplied-alpha compositing: both VG fragment shaders output premultiplied color
            // (rgb *= a). This removes the dark halo on straight-alpha AA edges and the double-blend
            // seams at fringe/join overlaps. (Full self-overlap correctness arrives with the planned
            // stencil-then-cover fill; this is the correct compositing foundation for it.)
            colorTarget.blend = rhi::BlendState::PremultipliedAlpha();
            const rhi::ColorTargetState colorTargets[1] = {colorTarget};

            rhi::RenderPipelineDesc desc{};
            desc.layout = m_pipelineLayout;
            desc.vertex.shader =
                rhi::ProgrammableStage{&vertShader, u8"main", rhi::ShaderStage::Vertex};
            desc.vertex.buffers = Span<const rhi::VertexBufferLayout>(vertexBuffers, 1);

            rhi::FragmentState fragment{};
            fragment.shader =
                rhi::ProgrammableStage{&fragShader, u8"main", rhi::ShaderStage::Fragment};
            fragment.targets = Span<const rhi::ColorTargetState>(colorTargets, 1);
            desc.fragment = fragment;

            desc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
            desc.primitive.frontFace = rhi::FrontFace::CCW;
            desc.primitive.cullMode = rhi::CullMode::None;
            desc.multisample.count = 1;
            desc.multisample.alphaToCoverageEnabled = false;

            return m_device->CreateRenderPipeline(desc, outPipeline);
        }

        Status CreatePerFrameResources()
        {
            m_vertexBuffers.Resize(static_cast<usize>(m_frameCount));
            m_indexBuffers.Resize(static_cast<usize>(m_frameCount));
            m_uniformBuffers.Resize(static_cast<usize>(m_frameCount));

            for (i32 i = 0; i < m_frameCount; ++i)
            {
                rhi::BufferDesc vd{};
                vd.size = static_cast<u64>(MaxVertices) * sizeof(VGRenderVertex);
                vd.usage = rhi::BufferUsage::Vertex;
                vd.memory = rhi::MemoryLocation::CpuToGpu;
                if (!m_device->CreateBuffer(vd, m_vertexBuffers[static_cast<usize>(i)]).IsOk())
                    return ErrorCode::Unknown;

                rhi::BufferDesc id{};
                id.size = static_cast<u64>(MaxIndices) * sizeof(u32);
                id.usage = rhi::BufferUsage::Index;
                id.memory = rhi::MemoryLocation::CpuToGpu;
                if (!m_device->CreateBuffer(id, m_indexBuffers[static_cast<usize>(i)]).IsOk())
                    return ErrorCode::Unknown;

                rhi::BufferDesc ud{};
                ud.size = static_cast<u64>(MaxUniformSlots) * UniformSlotSize;
                ud.usage = rhi::BufferUsage::Uniform;
                ud.memory = rhi::MemoryLocation::CpuToGpu;
                if (!m_device->CreateBuffer(ud, m_uniformBuffers[static_cast<usize>(i)]).IsOk())
                    return ErrorCode::Unknown;
            }
            return ErrorCode::Ok;
        }

        CachedTexture* GetOrCreateCachedTexture(const image::ImageData* texture)
        {
            if (texture == nullptr)
                return nullptr;

            for (usize i = 0; i < m_textureCache.Size(); ++i)
                if (m_textureCache[i]->source == texture)
                    return m_textureCache[i].Get();

            const Span<const u8> pixels = texture->PixelData();
            if (pixels.Size() == 0)
                return nullptr;

            const u32 w = texture->Width();
            const u32 h = texture->Height();
            const rhi::TextureFormat fmt = draconic::texture::TextureFormatUtils::Convert(
                texture->Format(), texture->ColorSpace());

            rhi::TextureDesc td{};
            td.dimension = rhi::TextureDimension::Texture2D;
            td.format = fmt;
            td.width = w;
            td.height = h;
            td.depth = 1;
            td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
            td.label = u8"VGRenderer cached texture";

            rhi::Texture* gpuTexture = nullptr;
            if (!m_device->CreateTexture(td, gpuTexture).IsOk())
                return nullptr;

            if (m_queue != nullptr)
            {
                rhi::TransferBatch* batch = nullptr;
                if (m_queue->CreateTransferBatch(batch).IsOk() && batch != nullptr)
                {
                    rhi::TextureDataLayout layout{};
                    layout.bytesPerRow = w * image::BytesPerPixel(texture->Format());
                    layout.rowsPerImage = h;
                    batch->WriteTexture(gpuTexture, pixels, layout, rhi::Extent3D{w, h, 1});
                    (void)batch->Submit();
                    m_queue->DestroyTransferBatch(batch);
                }
            }

            rhi::TextureViewDesc vd{};
            vd.format = fmt;
            rhi::TextureView* view = nullptr;
            if (!m_device->CreateTextureView(gpuTexture, vd, view).IsOk())
            {
                m_device->DestroyTexture(gpuTexture);
                return nullptr;
            }

            UniquePtr<CachedTexture> cached = MakeUnique<CachedTexture>(DefaultAllocator());
            cached->source = texture;
            cached->gpuTexture = gpuTexture;
            cached->view = view;
            cached->bindGroups.Resize(static_cast<usize>(m_frameCount)); // nullptr-filled
            CachedTexture* raw = cached.Get();
            m_textureCache.PushBack(Move(cached));
            return raw;
        }

        void UpdateTextureBindGroup(i32 textureIndex, i32 frameIndex)
        {
            if (textureIndex >= static_cast<i32>(m_batchTextures.Size()))
                return;
            const image::ImageData* texture = m_batchTextures[static_cast<usize>(textureIndex)];
            if (texture == nullptr)
                return;

            CachedTexture* cached = GetOrCreateCachedTexture(texture);
            if (cached == nullptr || cached->view == nullptr)
                return;
            if (cached->bindGroups[static_cast<usize>(frameIndex)] != nullptr)
                return; // already built

            rhi::BindGroupEntry entries[3];
            entries[0] = rhi::BindGroupEntry::BufferEntry(
                m_uniformBuffers[static_cast<usize>(frameIndex)], 0, sizeof(VGUniforms));
            entries[1] = rhi::BindGroupEntry::TextureEntry(cached->view);
            entries[2] = rhi::BindGroupEntry::SamplerEntry(m_sampler);

            rhi::BindGroupDesc desc{};
            desc.layout = m_bindGroupLayout;
            desc.entries = Span<const rhi::BindGroupEntry>(entries, 3);
            rhi::BindGroup* group = nullptr;
            if (m_device->CreateBindGroup(desc, group).IsOk())
                cached->bindGroups[static_cast<usize>(frameIndex)] = group;
        }

        rhi::BindGroup* GetBindGroupForTexture(i32 textureIndex, i32 frameIndex)
        {
            if (m_batchTextures.IsEmpty())
                return nullptr;
            const i32 effectiveIndex =
                (textureIndex < 0) ? 0 : textureIndex; // solid draws -> white at 0
            if (effectiveIndex >= static_cast<i32>(m_batchTextures.Size()))
                return nullptr;

            const image::ImageData* texture = m_batchTextures[static_cast<usize>(effectiveIndex)];
            if (texture == nullptr)
                return nullptr;

            for (usize i = 0; i < m_textureCache.Size(); ++i)
                if (m_textureCache[i]->source == texture)
                    return m_textureCache[i]->bindGroups[static_cast<usize>(frameIndex)];
            return nullptr;
        }

        void DisposeCachedTexture(CachedTexture& cached)
        {
            for (usize i = 0; i < cached.bindGroups.Size(); ++i)
                if (cached.bindGroups[i] != nullptr)
                    m_device->DestroyBindGroup(cached.bindGroups[i]);
            if (cached.external)
                return; // view/texture are caller-owned
            if (cached.view)
                m_device->DestroyTextureView(cached.view);
            if (cached.gpuTexture)
                m_device->DestroyTexture(cached.gpuTexture);
        }

        void DestroyBuffers(Array<rhi::Buffer*>& buffers)
        {
            for (usize i = 0; i < buffers.Size(); ++i)
                if (buffers[i] != nullptr)
                    m_device->DestroyBuffer(buffers[i]);
            buffers.Clear();
        }

        rhi::Device* m_device = nullptr;
        rhi::Queue* m_queue = nullptr;
        i32 m_frameCount = 0;
        rhi::TextureFormat m_targetFormat = rhi::TextureFormat::BGRA8UnormSrgb;

        rhi::BindGroupLayout* m_bindGroupLayout = nullptr;
        rhi::PipelineLayout* m_pipelineLayout = nullptr;
        rhi::RenderPipeline* m_pipeline = nullptr;
        rhi::RenderPipeline* m_dfPipeline = nullptr; // MSDF fragment variant (null if unused)
        rhi::Sampler* m_sampler = nullptr;

        Array<rhi::Buffer*> m_vertexBuffers;
        Array<rhi::Buffer*> m_indexBuffers;
        Array<rhi::Buffer*> m_uniformBuffers;

        Array<UniquePtr<CachedTexture>> m_textureCache;
        Array<const image::ImageData*> m_batchTextures;
        Array<draconic::vg::VGCommand> m_drawCommands;

        Array<u32> m_frameVertexOffsets;
        Array<u32> m_frameIndexOffsets;
        Array<u32> m_frameUniformSlotCount;

        bool m_initialized = false;
    };
}
