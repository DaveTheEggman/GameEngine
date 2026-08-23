// engine.terrain:renderer - the chunked geo-mipmap terrain Renderer.
//
// Rides the Opaque category via the RegisterRenderer seam (no terrain code in Engine.Render, the
// sprites/particles precedent). ONE 65x65 grid VB + one index buffer PER LOD are uploaded once; every
// chunk draws that shared grid, placed + height-displaced in the VS from a per-chunk uniform and the
// R16Uint height texture. Resolve is where the per-view work lives (Resolve alone has the camera): it
// folds each terrain's world transform into a per-terrain ViewProj, replays foundation.terrain's
// ExtractVisibleChunkDraws (quadtree cull + shared-coverage LOD - the same tested CPU path), and emits
// one indexed draw per visible chunk at its LOD. Bind sets: 0 view, 1 per-chunk, 2 height texture.

module;
#include "Core/Prelude.h"

export module engine.terrain:renderer;

import foundation.core;
import foundation.rhi;
import foundation.shaders;
import foundation.shaders.system;
import foundation.render; // Renderer, RenderRecordContext, ResolvedDraw, DrawItem, DynamicUniformRing
import foundation.terrain; // chunk grid mesh + ExtractVisibleChunkDraws (cull + LOD)
import :renderdata;

using namespace foundation::core;
namespace core = foundation::core;
namespace rhi = foundation::rhi;
namespace shaders = foundation::shaders;

export namespace engine::terrain
{
    namespace render = foundation::render;
    namespace tmodel = foundation::terrain;

    class TerrainRenderer final : public render::Renderer
    {
    public:
        TerrainRenderer(rhi::Device& device, shaders::ShaderSystem& shaders,
                        u32 framesInFlight) noexcept
            : m_device(&device), m_shaders(&shaders),
              m_viewRing(device, framesInFlight, kViewSlotSize,
                         rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, u8"terrain.view"),
              m_chunkRing(device, framesInFlight, kChunkSlotSize,
                          rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, u8"terrain.chunk")
        {
        }
        ~TerrainRenderer() override { Shutdown(); }
        TerrainRenderer(const TerrainRenderer&) = delete;
        TerrainRenderer& operator=(const TerrainRenderer&) = delete;

        core::Status Initialize()
        {
            // set 0: per-terrain view UBO (chunkToWorld folded into ViewProj), dynamic offset.
            rhi::BindGroupLayoutEntry viewEntry = rhi::BindGroupLayoutEntry::UniformBuffer(
                0, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment);
            viewEntry.hasDynamicOffset = true;
            rhi::BindGroupLayoutDesc vld{};
            vld.entries = Span<const rhi::BindGroupLayoutEntry>{&viewEntry, 1};
            if (!m_device->CreateBindGroupLayout(vld, m_viewLayout).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }

            // set 1: per-chunk placement UBO, dynamic offset (one slot per visible chunk).
            rhi::BindGroupLayoutEntry chunkEntry = rhi::BindGroupLayoutEntry::UniformBuffer(
                0, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment);
            chunkEntry.hasDynamicOffset = true;
            rhi::BindGroupLayoutDesc cld{};
            cld.entries = Span<const rhi::BindGroupLayoutEntry>{&chunkEntry, 1};
            if (!m_device->CreateBindGroupLayout(cld, m_chunkLayout).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }

            // set 2: the R16Uint height texture, read via Load in the VS (and PS for the normal).
            rhi::BindGroupLayoutEntry heightEntry = rhi::BindGroupLayoutEntry::SampledTexture(
                0, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment);
            heightEntry.textureSampleType = rhi::TextureSampleType::Uint; // WebGPU: integer data texture
            rhi::BindGroupLayoutDesc hld{};
            hld.entries = Span<const rhi::BindGroupLayoutEntry>{&heightEntry, 1};
            if (!m_device->CreateBindGroupLayout(hld, m_heightLayout).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }

            rhi::BindGroupLayout* layouts[] = {m_viewLayout, m_chunkLayout, m_heightLayout};
            rhi::PipelineLayoutDesc pld{};
            pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{layouts, 3};
            if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }

            // The shared grid vertex buffer (surface + skirt copies, each vertex = u,v,skirtFlag) -
            // uploaded once, drawn for every chunk.
            Array<Float3> verts;
            tmodel::BuildChunkGridVertices(verts);
            m_gridVertexCount = static_cast<u32>(verts.Size());
            rhi::BufferDesc vbd{};
            vbd.size = verts.Size() * sizeof(Float3);
            vbd.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
            vbd.memory = rhi::MemoryLocation::CpuToGpu;
            vbd.label = u8"terrain.grid.verts";
            if (!m_device->CreateBuffer(vbd, m_gridVertexBuffer).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }
            if (void* p = m_gridVertexBuffer->Map())
            {
                MemCopy(p, verts.Data(), verts.Size() * sizeof(Float3));
                m_gridVertexBuffer->Unmap();
            }

            // One index buffer per LOD (stride 2^lod over the shared grid).
            for (u32 lod = 0; lod <= tmodel::kMaxChunkLod; ++lod)
            {
                Array<u32> indices;
                tmodel::BuildChunkGridIndices(lod, indices);
                LodMesh& lm = m_lodMeshes[lod];
                lm.indexCount = static_cast<u32>(indices.Size());
                lm.surfaceIndexCount = tmodel::ChunkLodSurfaceIndexCount(lod);
                if (lm.indexCount == 0)
                {
                    continue;
                }
                rhi::BufferDesc ibd{};
                ibd.size = indices.Size() * sizeof(u32);
                ibd.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
                ibd.memory = rhi::MemoryLocation::CpuToGpu;
                ibd.label = u8"terrain.grid.indices";
                if (!m_device->CreateBuffer(ibd, lm.indexBuffer).IsOk())
                {
                    return core::Status{core::ErrorCode::Unknown};
                }
                if (void* p = lm.indexBuffer->Map())
                {
                    MemCopy(p, indices.Data(), indices.Size() * sizeof(u32));
                    lm.indexBuffer->Unmap();
                }
            }
            return core::Status{};
        }

        [[nodiscard]] Span<const render::RenderCategory> SupportedCategories() const override
        {
            static const render::RenderCategory cats[] = {render::RenderCategories::Opaque};
            return Span<const render::RenderCategory>{cats, 1};
        }

        void PrepareFrame(u32 maxDraws, u32 frameIndex) override
        {
            const u32 chunk = 4096u;
            m_viewRing.Reserve(Max(kMaxTerrains, maxDraws == 0 ? 1u : maxDraws));
            const u32 wantChunks = ((m_maxChunksSeen + chunk - 1u) / chunk) * chunk;
            m_chunkRing.Reserve(wantChunks == 0 ? chunk : wantChunks);
            m_viewRing.BeginFrame(frameIndex);
            m_chunkRing.BeginFrame(frameIndex);
            m_frameChunks = 0;
        }

        void Resolve(const render::RenderRecordContext& ctx, Span<const render::DrawItem> items,
                     Array<render::ResolvedDraw>& out) override
        {
            if (items.IsEmpty())
            {
                return;
            }
            m_depthFormat = ctx.depthFormat;
            rhi::RenderPipeline* pso = EnsurePipeline(ctx.colorFormat);
            if (pso == nullptr)
            {
                return;
            }
            rhi::BindGroup* viewBg = EnsureViewBindGroup();
            rhi::BindGroup* chunkBg = EnsureChunkBindGroup();
            if (viewBg == nullptr || chunkBg == nullptr)
            {
                return;
            }

            const Float4x4 proj =
                (ctx.view != nullptr) ? ctx.view->Camera().projection : Float4x4::Identity();
            // The scene's sun: the first directional light (GpuLight::type 0), as a direction TO the
            // light. Falls back to a fixed key light when the scene has no directional (so terrain is
            // never unlit). Point/spot lights are ignored here - terrain takes only the sun (Phase E).
            Float3 sun = Normalized(Float3{0.35f, 0.82f, 0.45f});
            for (usize li = 0; li < ctx.lights.Size(); ++li)
            {
                const render::GpuLight& light = ctx.lights[li];
                if (light.type < 0.5f) // directional
                {
                    const f32 len = Length(light.directionWS);
                    if (len > 1.0e-4f)
                    {
                        sun = light.directionWS * (-1.0f / len); // travel dir -> dir TO light
                    }
                    break;
                }
            }

            for (usize it = 0; it < items.Size(); ++it)
            {
                const auto* data = static_cast<const TerrainRenderData*>(items[it].data);
                if (data == nullptr || data->chunks == nullptr || data->quadtree == nullptr ||
                    data->heightView == nullptr || data->chunkCount == 0)
                {
                    continue;
                }

                // Per-terrain view UBO: fold the terrain's world transform into ViewProj so the VS
                // renders heightfield-local positions straight to clip.
                const render::DynamicUniformRing::Range vr = m_viewRing.Allocate();
                if (!vr.ok)
                {
                    continue;
                }
                struct ViewUBO
                {
                    Float4x4 viewProj;
                    Float4 lightDir;
                    Float4 cameraPos;
                } ubo{data->chunkToWorld * ctx.viewProj, Float4{sun.x, sun.y, sun.z, 0.0f},
                      Float4{ctx.cameraPos.x, ctx.cameraPos.y, ctx.cameraPos.z, 0.0f}};
                MemCopy(vr.ptr, &ubo, sizeof(ubo));

                // Local-space frustum (chunkToWorld folded in) matches the chunks' local bounds.
                const BoundingFrustum frustum(data->chunkToWorld * ctx.viewProj);
                const Span<const tmodel::TerrainChunk> chunks{data->chunks, data->chunkCount};
                const Span<const f32> thresholds{data->thresholds, data->thresholdCount};
                m_draws.Clear();
                tmodel::ExtractVisibleChunkDraws(*data->quadtree, chunks, data->chunkToWorld,
                                                 ctx.viewMatrix, proj, frustum, thresholds,
                                                 data->lodBias, m_draws);
                if (m_draws.IsEmpty())
                {
                    continue;
                }

                rhi::BindGroup* heightBg = EnsureHeightBindGroup(data->heightView);
                if (heightBg == nullptr)
                {
                    continue;
                }

                for (usize d = 0; d < m_draws.Size(); ++d)
                {
                    const tmodel::ChunkDraw& cd = m_draws[d];
                    const u32 lod = Min(cd.lod, tmodel::kMaxChunkLod);
                    const LodMesh& lm = m_lodMeshes[lod];
                    if (lm.indexBuffer == nullptr || lm.indexCount == 0)
                    {
                        continue;
                    }
                    const tmodel::TerrainChunk& c = data->chunks[static_cast<usize>(cd.chunkIndex)];

                    const render::DynamicUniformRing::Range cr = m_chunkRing.Allocate();
                    if (!cr.ok)
                    {
                        continue;
                    }
                    ++m_frameChunks;
                    // Skirt depth: how far the skirt ring drops below the surface to plug an LOD
                    // seam. Bounded by the chunk's own relief (a seam can't mismatch by more), with a
                    // small floor for near-flat terrain. Skirts are only visible AT a crack, so being
                    // generous is free.
                    const f32 skirtDepth =
                        Max(1.0f, 0.5f * (c.bounds.max.y - c.bounds.min.y));
                    struct ChunkUBO
                    {
                        Float2 originXZ;
                        Float2 sizeXZ;
                        Float2 texelBase;
                        Float2 texelSpan;
                        Float2 heightRange;
                        Float2 gridSize;
                        Float2 skirt; // x = skirt depth (world), y = pad
                    } cb{Float2{c.bounds.min.x, c.bounds.min.z},
                         Float2{c.bounds.max.x - c.bounds.min.x, c.bounds.max.z - c.bounds.min.z},
                         Float2{static_cast<f32>(c.gridX0), static_cast<f32>(c.gridZ0)},
                         Float2{static_cast<f32>(tmodel::kChunkQuads),
                                static_cast<f32>(tmodel::kChunkQuads)},
                         Float2{data->minY, data->maxY},
                         Float2{static_cast<f32>(data->gridSize),
                                static_cast<f32>(data->gridSize)},
                         Float2{skirtDepth, 0.0f}};
                    MemCopy(cr.ptr, &cb, sizeof(cb));

                    render::ResolvedDraw draw{};
                    draw.pso = pso;
                    draw.viewSet = viewBg;
                    draw.viewDynamic = true;
                    draw.viewOffset = vr.byteOffset;
                    draw.drawSet = chunkBg;
                    draw.drawDynamic = true;
                    draw.drawOffset = cr.byteOffset;
                    draw.materialSet = heightBg; // set 2: height texture
                    draw.vertexBuffer0 = m_gridVertexBuffer;
                    draw.indexBuffer = lm.indexBuffer;
                    draw.indexFormat = rhi::IndexFormat::UInt32;
                    // Skirtless pass draws only the surface prefix (crack-plug diagnostics/tests).
                    draw.indexCount = m_skirtsEnabled ? lm.indexCount : lm.surfaceIndexCount;
                    draw.instanceCount = 1;
                    out.PushBack(draw);
                }
            }
        }

        void FinishFrame() override
        {
            m_maxChunksSeen = Max(m_maxChunksSeen, m_frameChunks);
            m_viewRing.EndFrame();
            m_chunkRing.EndFrame();
        }

        void SetRetireQueue(render::GpuRetireQueue* retire) noexcept
        {
            m_viewRing.SetRetireQueue(retire);
            m_chunkRing.SetRetireQueue(retire);
        }

        /// Peak per-frame chunk-draw count seen so far (diagnostics + headless verification): > 0 only
        /// once a frame emitted terrain draws, which requires the PSO (hence the shaders) to have built.
        [[nodiscard]] u32 MaxChunksDrawn() const noexcept { return m_maxChunksSeen; }

        /// Draw the LOD-seam skirts (default on). Off draws only the surface - used to prove the skirts
        /// actually plug cracks (a skirtless mixed-LOD frame leaks background through the seams).
        void SetSkirtsEnabled(bool enabled) noexcept { m_skirtsEnabled = enabled; }

    private:
        static constexpr u32 kMaxTerrains = 8;
        static constexpr u64 kViewSlotSize = 256;  // mat4 + 2 float4, padded to dynamic alignment
        static constexpr u64 kChunkSlotSize = 256; // 6 float2, padded to dynamic alignment

        struct LodMesh
        {
            rhi::Buffer* indexBuffer = nullptr;
            u32 indexCount = 0;        // surface + skirt walls
            u32 surfaceIndexCount = 0; // the surface prefix (skirtless draw range)
        };

        rhi::BindGroup* EnsureViewBindGroup()
        {
            const u32 gen = m_viewRing.Generation();
            if (m_viewBg != nullptr && m_viewBgGen == gen)
            {
                return m_viewBg;
            }
            if (m_viewBg != nullptr)
            {
                m_device->DestroyBindGroup(m_viewBg);
                m_viewBg = nullptr;
            }
            if (m_viewRing.Buffer() == nullptr)
            {
                return nullptr;
            }
            rhi::BindGroupEntry e =
                rhi::BindGroupEntry::BufferEntry(m_viewRing.Buffer(), 0, kViewSlotSize);
            rhi::BindGroupDesc bgd{};
            bgd.layout = m_viewLayout;
            bgd.entries = Span<const rhi::BindGroupEntry>{&e, 1};
            if (!m_device->CreateBindGroup(bgd, m_viewBg).IsOk())
            {
                m_viewBg = nullptr;
                return nullptr;
            }
            m_viewBgGen = gen;
            return m_viewBg;
        }

        rhi::BindGroup* EnsureChunkBindGroup()
        {
            const u32 gen = m_chunkRing.Generation();
            if (m_chunkBg != nullptr && m_chunkBgGen == gen)
            {
                return m_chunkBg;
            }
            if (m_chunkBg != nullptr)
            {
                m_device->DestroyBindGroup(m_chunkBg);
                m_chunkBg = nullptr;
            }
            if (m_chunkRing.Buffer() == nullptr)
            {
                return nullptr;
            }
            rhi::BindGroupEntry e =
                rhi::BindGroupEntry::BufferEntry(m_chunkRing.Buffer(), 0, kChunkSlotSize);
            rhi::BindGroupDesc bgd{};
            bgd.layout = m_chunkLayout;
            bgd.entries = Span<const rhi::BindGroupEntry>{&e, 1};
            if (!m_device->CreateBindGroup(bgd, m_chunkBg).IsOk())
            {
                m_chunkBg = nullptr;
                return nullptr;
            }
            m_chunkBgGen = gen;
            return m_chunkBg;
        }

        // Per-height-texture bind group (set 2), keyed by view, validated by uniqueId (address reuse).
        rhi::BindGroup* EnsureHeightBindGroup(rhi::TextureView* tex)
        {
            if (tex == nullptr)
            {
                return nullptr;
            }
            if (HeightBindGroup* found = m_heightBindGroups.Find(tex))
            {
                if (found->viewId == tex->uniqueId)
                {
                    return found->bindGroup;
                }
                if (found->bindGroup != nullptr)
                {
                    m_device->DestroyBindGroup(found->bindGroup);
                }
                m_heightBindGroups.Remove(tex);
            }
            rhi::BindGroupEntry e = rhi::BindGroupEntry::TextureEntry(tex);
            rhi::BindGroupDesc bgd{};
            bgd.layout = m_heightLayout;
            bgd.entries = Span<const rhi::BindGroupEntry>{&e, 1};
            rhi::BindGroup* bg = nullptr;
            if (!m_device->CreateBindGroup(bgd, bg).IsOk())
            {
                return nullptr;
            }
            m_heightBindGroups.InsertOrAssign(tex, HeightBindGroup{bg, tex->uniqueId});
            return bg;
        }

        rhi::RenderPipeline* EnsurePipeline(rhi::TextureFormat colorFormat)
        {
            const u64 shaderVersion = m_shaders->Version(u8"terrain");
            if (m_pso != nullptr && m_psoFormat == colorFormat && m_psoShaderVersion == shaderVersion)
            {
                return m_pso;
            }
            if (m_pso != nullptr)
            {
                m_device->DestroyRenderPipeline(m_pso);
                m_pso = nullptr;
            }
            rhi::ShaderModule* vs = m_shaders->GetVariant(u8"terrain", shaders::ShaderStage::Vertex,
                                                          shaders::ShaderFlags::None);
            rhi::ShaderModule* ps = m_shaders->GetVariant(u8"terrain", shaders::ShaderStage::Fragment,
                                                          shaders::ShaderFlags::None);
            if (vs == nullptr || ps == nullptr)
            {
                return nullptr;
            }

            const rhi::VertexAttribute attrs[] = {{rhi::VertexFormat::Float32x3, 0, 0}};
            rhi::VertexBufferLayout vbl{};
            vbl.stride = sizeof(Float3);
            vbl.stepMode = rhi::VertexStepMode::Vertex;
            vbl.attributes = Span<const rhi::VertexAttribute>{attrs, 1};

            rhi::ColorTargetState target{};
            target.format = colorFormat; // opaque: no blend

            rhi::FragmentState frag{};
            frag.shader = rhi::ProgrammableStage{ps, u8"main", rhi::ShaderStage::Fragment};
            frag.targets = Span<const rhi::ColorTargetState>{&target, 1};

            rhi::DepthStencilState ds{};
            ds.format = m_depthFormat;
            ds.depthTestEnabled = true;
            ds.depthWriteEnabled = true;
            ds.depthCompare = rhi::CompareFunction::LessEqual;

            rhi::RenderPipelineDesc pd{};
            pd.layout = m_pipelineLayout;
            pd.vertex.shader = rhi::ProgrammableStage{vs, u8"main", rhi::ShaderStage::Vertex};
            pd.vertex.buffers = Span<const rhi::VertexBufferLayout>{&vbl, 1};
            pd.fragment = frag;
            pd.depthStencil = ds;
            pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
            // The grid winds CCW when seen from above (the default FrontFace::CCW), so the top
            // surface is the front face: cull backs. Verified by the Vulkan pixel probe (a wrong
            // choice culls the top surface and the frame goes black).
            pd.primitive.cullMode = rhi::CullMode::Back;
            pd.label = u8"terrain";
            rhi::RenderPipeline* pso = nullptr;
            if (!m_device->CreateRenderPipeline(pd, pso).IsOk())
            {
                return nullptr;
            }
            m_pso = pso;
            m_psoFormat = colorFormat;
            m_psoShaderVersion = shaderVersion;
            return pso;
        }

        void Shutdown()
        {
            for (auto& kv : m_heightBindGroups)
            {
                if (kv.value.bindGroup != nullptr)
                {
                    m_device->DestroyBindGroup(kv.value.bindGroup);
                }
            }
            m_heightBindGroups.Clear();
            if (m_viewBg != nullptr)
            {
                m_device->DestroyBindGroup(m_viewBg);
                m_viewBg = nullptr;
            }
            if (m_chunkBg != nullptr)
            {
                m_device->DestroyBindGroup(m_chunkBg);
                m_chunkBg = nullptr;
            }
            if (m_pso != nullptr)
            {
                m_device->DestroyRenderPipeline(m_pso);
                m_pso = nullptr;
            }
            for (LodMesh& lm : m_lodMeshes)
            {
                if (lm.indexBuffer != nullptr)
                {
                    m_device->DestroyBuffer(lm.indexBuffer);
                    lm.indexBuffer = nullptr;
                }
            }
            if (m_gridVertexBuffer != nullptr)
            {
                m_device->DestroyBuffer(m_gridVertexBuffer);
                m_gridVertexBuffer = nullptr;
            }
            if (m_pipelineLayout != nullptr)
            {
                m_device->DestroyPipelineLayout(m_pipelineLayout);
                m_pipelineLayout = nullptr;
            }
            if (m_heightLayout != nullptr)
            {
                m_device->DestroyBindGroupLayout(m_heightLayout);
                m_heightLayout = nullptr;
            }
            if (m_chunkLayout != nullptr)
            {
                m_device->DestroyBindGroupLayout(m_chunkLayout);
                m_chunkLayout = nullptr;
            }
            if (m_viewLayout != nullptr)
            {
                m_device->DestroyBindGroupLayout(m_viewLayout);
                m_viewLayout = nullptr;
            }
        }

        struct HeightBindGroup
        {
            rhi::BindGroup* bindGroup = nullptr;
            u64 viewId = 0;
        };

        rhi::Device* m_device;
        shaders::ShaderSystem* m_shaders;
        render::DynamicUniformRing m_viewRing;
        render::DynamicUniformRing m_chunkRing;
        rhi::BindGroupLayout* m_viewLayout = nullptr;
        rhi::BindGroupLayout* m_chunkLayout = nullptr;
        rhi::BindGroupLayout* m_heightLayout = nullptr;
        rhi::PipelineLayout* m_pipelineLayout = nullptr;
        rhi::Buffer* m_gridVertexBuffer = nullptr;
        u32 m_gridVertexCount = 0;
        LodMesh m_lodMeshes[tmodel::kMaxChunkLod + 1];
        rhi::BindGroup* m_viewBg = nullptr;
        u32 m_viewBgGen = 0;
        rhi::BindGroup* m_chunkBg = nullptr;
        u32 m_chunkBgGen = 0;
        HashMap<rhi::TextureView*, HeightBindGroup> m_heightBindGroups;
        rhi::RenderPipeline* m_pso = nullptr;
        rhi::TextureFormat m_psoFormat = rhi::TextureFormat::Undefined;
        u64 m_psoShaderVersion = 0;
        rhi::TextureFormat m_depthFormat = rhi::TextureFormat::Undefined;
        Array<tmodel::ChunkDraw> m_draws; // scratch, reused each terrain (Resolve is single-threaded)
        u32 m_frameChunks = 0;
        u32 m_maxChunksSeen = 0;
        bool m_skirtsEnabled = true;
    };
}
