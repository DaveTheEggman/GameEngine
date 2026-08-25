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
            // set 0: per-terrain view UBO (b0, dynamic) + the CSM cascade array (t1) + a comparison
            // sampler (s0). The depth caster pass ignores t1/s0 (it only reads b0), but sharing one
            // layout keeps a single pipeline layout for both passes.
            rhi::BindGroupLayoutEntry viewEntry = rhi::BindGroupLayoutEntry::UniformBuffer(
                0, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment);
            viewEntry.hasDynamicOffset = true;
            rhi::BindGroupLayoutEntry shadowTexEntry = rhi::BindGroupLayoutEntry::SampledTexture(
                1, rhi::ShaderStage::Fragment, rhi::TextureViewDimension::Texture2DArray);
            shadowTexEntry.textureSampleType = rhi::TextureSampleType::Depth;
            rhi::BindGroupLayoutEntry shadowSampEntry{};
            shadowSampEntry.binding = 0;
            shadowSampEntry.visibility = rhi::ShaderStage::Fragment;
            shadowSampEntry.type = rhi::BindingType::ComparisonSampler;
            rhi::BindGroupLayoutEntry viewEntries[] = {viewEntry, shadowTexEntry, shadowSampEntry};
            rhi::BindGroupLayoutDesc vld{};
            vld.entries = Span<const rhi::BindGroupLayoutEntry>{viewEntries, 3};
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

            // set 3: the D2 splat material - RGBA weight map (t0) + up to 4 layer albedos (t1..t4) +
            // a clamp/bilinear splat sampler (s0) + a repeat/trilinear albedo sampler (s1).
            rhi::BindGroupLayoutEntry matEntries[] = {
                rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment),
                rhi::BindGroupLayoutEntry::SampledTexture(1, rhi::ShaderStage::Fragment),
                rhi::BindGroupLayoutEntry::SampledTexture(2, rhi::ShaderStage::Fragment),
                rhi::BindGroupLayoutEntry::SampledTexture(3, rhi::ShaderStage::Fragment),
                rhi::BindGroupLayoutEntry::SampledTexture(4, rhi::ShaderStage::Fragment),
                rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment),
                rhi::BindGroupLayoutEntry::Sampler(1, rhi::ShaderStage::Fragment),
            };
            rhi::BindGroupLayoutDesc mld{};
            mld.entries = Span<const rhi::BindGroupLayoutEntry>{matEntries, 7};
            if (!m_device->CreateBindGroupLayout(mld, m_materialLayout).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }

            // Two pipeline layouts: the color pass binds set 3 (material); the depth pass does not (so
            // WebGPU's "every declared set must be bound" rule is satisfied without a spurious bind).
            rhi::BindGroupLayout* colorLayouts[] = {m_viewLayout, m_chunkLayout, m_heightLayout,
                                                    m_materialLayout};
            rhi::PipelineLayoutDesc pld{};
            pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{colorLayouts, 4};
            if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }
            rhi::BindGroupLayout* depthLayouts[] = {m_viewLayout, m_chunkLayout, m_heightLayout};
            rhi::PipelineLayoutDesc dpld{};
            dpld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{depthLayouts, 3};
            if (!m_device->CreatePipelineLayout(dpld, m_depthPipelineLayout).IsOk())
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

            // Shadow receive: a comparison sampler + a 1x1 dummy Texture2DArray depth map bound when no
            // caster exists this frame (so set 0 stays complete). The real CSM array arrives via
            // SetShadowMap. Mirrors MeshRenderer::CreateShadowResources.
            rhi::SamplerDesc ssd{};
            ssd.minFilter = rhi::FilterMode::Linear;
            ssd.magFilter = rhi::FilterMode::Linear;
            ssd.mipmapFilter = rhi::MipmapFilterMode::Nearest;
            ssd.addressU = rhi::AddressMode::ClampToEdge;
            ssd.addressV = rhi::AddressMode::ClampToEdge;
            ssd.addressW = rhi::AddressMode::ClampToEdge;
            ssd.compare = rhi::CompareFunction::LessEqual; // lit when fragment depth <= stored depth
            ssd.label = u8"terrain.shadowSampler";
            if (!m_device->CreateSampler(ssd, m_shadowSampler).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }
            rhi::TextureDesc dsd{};
            dsd.format = rhi::TextureFormat::Depth32Float;
            dsd.width = 1;
            dsd.height = 1;
            dsd.arrayLayerCount = 1;
            dsd.usage = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::Sampled;
            dsd.label = u8"terrain.dummyShadow";
            if (!m_device->CreateTexture(dsd, m_dummyShadowTex).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }
            rhi::TextureViewDesc dvd{};
            dvd.format = rhi::TextureFormat::Depth32Float;
            dvd.aspect = rhi::TextureAspect::DepthOnly;
            dvd.dimension = rhi::TextureViewDimension::Texture2DArray;
            dvd.arrayLayerCount = 1;
            if (!m_device->CreateTextureView(m_dummyShadowTex, dvd, m_dummyShadowView).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }
            m_activeShadowView = m_dummyShadowView;

            // Splat material samplers: splatmap = clamp + bilinear (soft weights), albedo = repeat +
            // trilinear (tiled, mip-sampled). A 1x1 white dummy fills absent splatmap/albedo slots.
            rhi::SamplerDesc spd{};
            spd.minFilter = rhi::FilterMode::Linear;
            spd.magFilter = rhi::FilterMode::Linear;
            spd.mipmapFilter = rhi::MipmapFilterMode::Nearest;
            spd.addressU = rhi::AddressMode::ClampToEdge;
            spd.addressV = rhi::AddressMode::ClampToEdge;
            spd.addressW = rhi::AddressMode::ClampToEdge;
            spd.label = u8"terrain.splatSampler";
            if (!m_device->CreateSampler(spd, m_splatSampler).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }
            rhi::SamplerDesc apd{};
            apd.minFilter = rhi::FilterMode::Linear;
            apd.magFilter = rhi::FilterMode::Linear;
            apd.mipmapFilter = rhi::MipmapFilterMode::Linear; // trilinear across the albedo mips
            apd.addressU = rhi::AddressMode::Repeat;
            apd.addressV = rhi::AddressMode::Repeat;
            apd.addressW = rhi::AddressMode::Repeat;
            apd.label = u8"terrain.albedoSampler";
            if (!m_device->CreateSampler(apd, m_albedoSampler).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }
            rhi::TextureDesc wtd{};
            wtd.format = rhi::TextureFormat::RGBA8Unorm;
            wtd.width = 1;
            wtd.height = 1;
            wtd.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
            wtd.label = u8"terrain.white";
            if (!m_device->CreateTexture(wtd, m_whiteTex).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }
            rhi::TextureViewDesc wvd{};
            wvd.format = rhi::TextureFormat::RGBA8Unorm;
            wvd.dimension = rhi::TextureViewDimension::Texture2D;
            if (!m_device->CreateTextureView(m_whiteTex, wvd, m_whiteView).IsOk())
            {
                return core::Status{core::ErrorCode::Unknown};
            }
            if (rhi::Queue* q = m_device->GetQueue(rhi::QueueType::Graphics))
            {
                rhi::TransferBatch* tb = nullptr;
                if (q->CreateTransferBatch(tb).IsOk() && tb != nullptr)
                {
                    const u8 white[4] = {255, 255, 255, 255};
                    rhi::TextureDataLayout layout{};
                    layout.bytesPerRow = 4;
                    layout.rowsPerImage = 1;
                    tb->WriteTexture(m_whiteTex, Span<const u8>{white, 4}, layout,
                                     rhi::Extent3D{1, 1, 1});
                    (void)tb->Submit();
                    q->DestroyTransferBatch(tb);
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
            // Each terrain allocates a view slot per PASS in a frame: 1 color + 1 depth prepass +
            // kCascadeCount shadow cascades (+ extra views). The chunk ring holds every pass's chunk
            // allocations (self-sized from the observed peak, both passes).
            const u32 wantViews =
                Max(kMaxTerrains * (2u + kCascadeCount), maxDraws == 0 ? 1u : maxDraws);
            m_viewRing.Reserve(wantViews);
            const u32 wantChunks = ((m_maxChunkAllocs + chunk - 1u) / chunk) * chunk;
            m_chunkRing.Reserve(wantChunks == 0 ? chunk : wantChunks);
            m_viewRing.BeginFrame(frameIndex);
            m_chunkRing.BeginFrame(frameIndex);
            m_frameChunks = 0;
            m_frameChunkAllocs = 0;
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
                if (data == nullptr || data->chunks == nullptr || data->nodes == nullptr ||
                    data->heightView == nullptr || data->chunkCount == 0)
                {
                    continue;
                }

                // Per-terrain view UBO. The VS works in WORLD space (ChunkToWorld applied there, NOT
                // folded into ViewProj) so the PS can shadow-sample worldPos against the world-space
                // cascade matrices. Cascade fields are filled from ctx.cascades (Stage B) - here they
                // stay zero => shadowMeta.x (cascade count) 0 => SampleCSM returns fully lit.
                const render::DynamicUniformRing::Range vr = m_viewRing.Allocate();
                if (!vr.ok)
                {
                    continue;
                }
                ViewUBO ubo{};
                ubo.chunkToWorld = data->chunkToWorld;
                ubo.viewProj = ctx.viewProj;
                ubo.view = ctx.viewMatrix;
                ubo.prevViewProj = ctx.prevViewProj;
                ubo.lightDir = Float4{sun.x, sun.y, sun.z, 0.0f};
                ubo.cameraPos = Float4{ctx.cameraPos.x, ctx.cameraPos.y, ctx.cameraPos.z, 0.0f};
                ubo.jitter = Float4{ctx.jitter.x, ctx.jitter.y, ctx.prevJitter.x, ctx.prevJitter.y};
                const f32 uvYSign = m_device->NeedsClipSpaceYFlip() ? 1.0f : -1.0f;
                ubo.shadowParams = Float4{0.0f, uvYSign, 0.0f, 0.0f};
                // CSM receive: copy this view's cascade matrices/splits (world-space - the VS emits
                // worldPos). Count 0 (no directional caster) leaves the PS fully lit. Mirrors
                // MeshRenderer's view-UBO shadow fill.
                if (ctx.cascades.valid)
                {
                    for (u32 c = 0; c < kCascadeCount; ++c)
                    {
                        ubo.cascadeViewProj[c] = ctx.cascades.viewProj[c];
                    }
                    ubo.cascadeSplitFar =
                        Float4{ctx.cascades.splitFar[0], ctx.cascades.splitFar[1],
                               ctx.cascades.splitFar[2], ctx.cascades.splitFar[3]};
                    ubo.cascadeTexelSize =
                        Float4{ctx.cascades.texelWorldSize[0], ctx.cascades.texelWorldSize[1],
                               ctx.cascades.texelWorldSize[2], ctx.cascades.texelWorldSize[3]};
                    ubo.shadowMeta = Float4{static_cast<f32>(kCascadeCount),
                                            static_cast<f32>(ctx.cascadeLayerBase),
                                            kShadowNormalBias, kShadowDepthBias};
                    ubo.shadowParams.x = ctx.shadowFarFade;
                }
                ubo.layerTileScales = Float4{data->tileScales[0], data->tileScales[1],
                                             data->tileScales[2], data->tileScales[3]};
                ubo.splatParams = Float4{static_cast<f32>(data->layerCount), 0.0f, 0.0f, 0.0f};
                MemCopy(vr.ptr, &ubo, sizeof(ubo));

                // Local-space frustum (chunkToWorld folded in) matches the chunks' local bounds.
                const BoundingFrustum frustum(data->chunkToWorld * ctx.viewProj);
                const Span<const tmodel::TerrainChunk> chunks{data->chunks, data->chunkCount};
                const Span<const f32> thresholds{data->thresholds, data->thresholdCount};
                m_draws.Clear();
                tmodel::ExtractVisibleChunkDraws(
                    Span<const tmodel::TerrainQuadtree::Node>{data->nodes, data->nodeCount}, chunks,
                    data->chunkToWorld, ctx.viewMatrix, proj, frustum, thresholds, data->lodBias,
                    m_draws);
                if (m_draws.IsEmpty())
                {
                    continue;
                }
                // Camera-independent pass (local-shadow tiles: ctx.view == null): the mesh
                // rule's terrain equivalent - every visible chunk casts at the COARSEST
                // level; shadows never render finer than any view shows. Cascades carry the
                // owning camera view (RecordShadowCasters), so their coverage LODs match the
                // main view's chunks exactly.
                if (ctx.view == nullptr)
                {
                    for (usize d = 0; d < m_draws.Size(); ++d)
                    {
                        m_draws[d].lod = tmodel::kMaxChunkLod;
                    }
                }

                rhi::BindGroup* heightBg = EnsureHeightBindGroup(data->heightView);
                rhi::BindGroup* materialBg =
                    EnsureMaterialBindGroup(data->splatmapView, data->albedoViews);
                if (heightBg == nullptr || materialBg == nullptr)
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
                    ++m_frameChunkAllocs;
                    ChunkUBO cb = MakeChunkUBO(*data, c);
                    MemCopy(cr.ptr, &cb, sizeof(cb));

                    render::ResolvedDraw draw{};
                    draw.pso = pso;
                    draw.viewSet = viewBg;
                    draw.viewDynamic = true;
                    draw.viewOffset = vr.byteOffset;
                    draw.drawSet = chunkBg;
                    draw.drawDynamic = true;
                    draw.drawOffset = cr.byteOffset;
                    draw.materialSet = heightBg;   // set 2: height texture
                    draw.clusterSet = materialBg;  // set 3: splat material (splatmap + albedos)
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

        // Depth-only caster: the camera depth prepass (ctx.depthPrepass) AND each CSM cascade (ctx.viewProj
        // = the cascade's world->light-clip). Same chunk cull/LOD as Resolve, but a vertex-only depth PSO
        // and SURFACE indices only (skirts are a shading crack-hack, not shadow casters).
        void ResolveDepthOnly(const render::RenderRecordContext& ctx, Span<const render::DrawItem> items,
                              Array<render::ResolvedDraw>& out) override
        {
            if (items.IsEmpty())
            {
                return;
            }
            m_depthFormat = ctx.depthFormat;
            rhi::RenderPipeline* pso = EnsureDepthPipeline(ctx.depthFormat, /*biased*/ !ctx.depthPrepass);
            rhi::BindGroup* viewBg = EnsureViewBindGroup();
            rhi::BindGroup* chunkBg = EnsureChunkBindGroup();
            if (pso == nullptr || viewBg == nullptr || chunkBg == nullptr)
            {
                return;
            }
            const Float4x4 proj =
                (ctx.view != nullptr) ? ctx.view->Camera().projection : Float4x4::Identity();

            for (usize it = 0; it < items.Size(); ++it)
            {
                const auto* data = static_cast<const TerrainRenderData*>(items[it].data);
                if (data == nullptr || data->chunks == nullptr || data->nodes == nullptr ||
                    data->heightView == nullptr || data->chunkCount == 0)
                {
                    continue;
                }
                const render::DynamicUniformRing::Range vr = m_viewRing.Allocate();
                if (!vr.ok)
                {
                    continue;
                }
                // The depth VS reads only ChunkToWorld + ViewProj; the rest of the slot is unused.
                ViewUBO ubo{};
                ubo.chunkToWorld = data->chunkToWorld;
                ubo.viewProj = ctx.viewProj; // camera VP (prepass) or cascade light VP (shadow)
                MemCopy(vr.ptr, &ubo, sizeof(ubo));

                const BoundingFrustum frustum(data->chunkToWorld * ctx.viewProj);
                const Span<const tmodel::TerrainChunk> chunks{data->chunks, data->chunkCount};
                const Span<const f32> thresholds{data->thresholds, data->thresholdCount};
                m_draws.Clear();
                tmodel::ExtractVisibleChunkDraws(
                    Span<const tmodel::TerrainQuadtree::Node>{data->nodes, data->nodeCount}, chunks,
                    data->chunkToWorld, ctx.viewMatrix, proj, frustum, thresholds, data->lodBias,
                    m_draws);
                if (m_draws.IsEmpty())
                {
                    continue;
                }
                // Camera-independent pass (local-shadow tiles: ctx.view == null): the mesh
                // rule's terrain equivalent - every visible chunk casts at the COARSEST
                // level; shadows never render finer than any view shows. Cascades carry the
                // owning camera view (RecordShadowCasters), so their coverage LODs match the
                // main view's chunks exactly.
                if (ctx.view == nullptr)
                {
                    for (usize d = 0; d < m_draws.Size(); ++d)
                    {
                        m_draws[d].lod = tmodel::kMaxChunkLod;
                    }
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
                    if (lm.indexBuffer == nullptr || lm.surfaceIndexCount == 0)
                    {
                        continue;
                    }
                    const tmodel::TerrainChunk& c = data->chunks[static_cast<usize>(cd.chunkIndex)];
                    const render::DynamicUniformRing::Range cr = m_chunkRing.Allocate();
                    if (!cr.ok)
                    {
                        continue;
                    }
                    ++m_frameChunkAllocs;
                    ChunkUBO cb = MakeChunkUBO(*data, c);
                    MemCopy(cr.ptr, &cb, sizeof(cb));

                    render::ResolvedDraw draw{};
                    draw.pso = pso;
                    draw.viewSet = viewBg;
                    draw.viewDynamic = true;
                    draw.viewOffset = vr.byteOffset;
                    draw.drawSet = chunkBg;
                    draw.drawDynamic = true;
                    draw.drawOffset = cr.byteOffset;
                    draw.materialSet = heightBg; // set 2: height texture (the VS displaces from it)
                    draw.vertexBuffer0 = m_gridVertexBuffer;
                    draw.indexBuffer = lm.indexBuffer;
                    draw.indexFormat = rhi::IndexFormat::UInt32;
                    draw.indexCount = lm.surfaceIndexCount; // surface only - no skirt casters
                    draw.instanceCount = 1;
                    out.PushBack(draw);
                }
            }
        }

        void FinishFrame() override
        {
            m_maxChunksSeen = Max(m_maxChunksSeen, m_frameChunks);
            m_maxChunkAllocs = Max(m_maxChunkAllocs, m_frameChunkAllocs);
            m_viewRing.EndFrame();
            m_chunkRing.EndFrame();
        }

        void SetRetireQueue(render::GpuRetireQueue* retire) noexcept
        {
            m_retire = retire; // stale height bind groups retire through it too (in-flight safety)
            m_viewRing.SetRetireQueue(retire);
            m_chunkRing.SetRetireQueue(retire);
        }

        // The frame's CSM cascade array (null = no caster this frame -> the dummy map, samples fully lit).
        // Fanned out to every registered renderer by RenderFrame each frame (before PrepareFrame).
        void SetShadowMap(rhi::TextureView* view, u64 generation) override
        {
            m_activeShadowView = (view != nullptr) ? view : m_dummyShadowView;
            m_activeShadowGen = (view != nullptr) ? generation : 0;
        }
        // Terrain samples only the directional CSM, not the local (spot/point) atlas - leave the base
        // SetShadowAtlas no-op.

        // One-time: transition the out-of-graph 1x1 dummy shadow depth into the layout its descriptor
        // expects (DepthStencilRead), so a caster-less frame never SAMPLES it while still UNDEFINED
        // (VUID-vkCmdDraw-None-09600). Terrain has no skinning; it uses this per-frame encoder hook
        // (the only Renderer callback holding the OUTER command encoder, before any render pass) purely
        // for that transition. Mirrors MeshRenderer::UploadSkinning.
        void UploadSkinning(const render::ExtractedScene&, rhi::CommandEncoder& encoder) override
        {
            if (!m_dummyDepthInit && m_dummyShadowTex != nullptr)
            {
                encoder.TransitionTexture(m_dummyShadowTex, rhi::ResourceState::Undefined,
                                          rhi::ResourceState::DepthStencilRead);
                m_dummyDepthInit = true;
            }
        }

        /// Peak per-frame chunk-draw count seen so far (diagnostics + headless verification): > 0 only
        /// once a frame emitted terrain draws, which requires the PSO (hence the shaders) to have built.
        [[nodiscard]] u32 MaxChunksDrawn() const noexcept { return m_maxChunksSeen; }

        /// Draw the LOD-seam skirts (default on). Off draws only the surface - used to prove the skirts
        /// actually plug cracks (a skirtless mixed-LOD frame leaks background through the seams).
        void SetSkirtsEnabled(bool enabled) noexcept { m_skirtsEnabled = enabled; }

    private:
        static constexpr u32 kMaxTerrains = 8;
        static constexpr u32 kCascadeCount = 4; // matches render::ShadowCascades::kCount
        static constexpr f32 kShadowNormalBias = 0.02f; // in texels (scaled by texelWorld in the PS)
        static constexpr f32 kShadowDepthBias = 0.0009f;
        static constexpr u64 kViewSlotSize = 768;  // 8 mat4 + 6 float4, padded to dynamic alignment
        static constexpr u64 kChunkSlotSize = 256; // 6 float2, padded to dynamic alignment

        // Mirrors the HLSL TerrainView cbuffer (b0, space0) - keep field order/offsets in lockstep.
        struct ViewUBO
        {
            Float4x4 chunkToWorld;
            Float4x4 viewProj;
            Float4x4 view;
            Float4x4 prevViewProj;
            Float4x4 cascadeViewProj[kCascadeCount];
            Float4 lightDir;
            Float4 cameraPos;
            Float4 jitter;
            Float4 cascadeSplitFar;
            Float4 cascadeTexelSize;
            Float4 shadowMeta;   // x = cascade count, y = layer base, z = normal bias, w = depth bias
            Float4 shadowParams; // x = far-fade width, y = uv.y sign, zw spare
            Float4 layerTileScales; // per-layer albedo tiling (local units per tile)
            Float4 splatParams;     // x = layer count (0 = height ramp), yzw spare
        };

        struct LodMesh
        {
            rhi::Buffer* indexBuffer = nullptr;
            u32 indexCount = 0;        // surface + skirt walls
            u32 surfaceIndexCount = 0; // the surface prefix (skirtless draw range)
        };

        struct DepthPso
        {
            rhi::RenderPipeline* pso = nullptr;
            rhi::TextureFormat format = rhi::TextureFormat::Undefined;
            u64 shaderVersion = 0;
        };

        // Mirrors the HLSL TerrainChunk cbuffer (b0, space1). Shared by the color + depth passes.
        struct ChunkUBO
        {
            Float2 originXZ;
            Float2 sizeXZ;
            Float2 texelBase;
            Float2 texelSpan;
            Float2 heightRange;
            Float2 gridSize;
            Float2 skirt; // x = skirt depth (world), y = pad
        };

        [[nodiscard]] static ChunkUBO MakeChunkUBO(const TerrainRenderData& data,
                                                   const tmodel::TerrainChunk& c)
        {
            // Skirt depth: how far the skirt ring drops below the surface to plug an LOD seam. Bounded
            // by the chunk's own relief (a seam can't mismatch by more), with a floor for near-flat
            // terrain. Only visible AT a crack, so being generous is free (unused in the depth pass).
            const f32 skirtDepth = Max(1.0f, 0.5f * (c.bounds.max.y - c.bounds.min.y));
            return ChunkUBO{
                Float2{c.bounds.min.x, c.bounds.min.z},
                Float2{c.bounds.max.x - c.bounds.min.x, c.bounds.max.z - c.bounds.min.z},
                Float2{static_cast<f32>(c.gridX0), static_cast<f32>(c.gridZ0)},
                Float2{static_cast<f32>(tmodel::kChunkQuads), static_cast<f32>(tmodel::kChunkQuads)},
                Float2{data.minY, data.maxY},
                Float2{static_cast<f32>(data.gridSize), static_cast<f32>(data.gridSize)},
                Float2{skirtDepth, 0.0f}};
        }

        rhi::BindGroup* EnsureViewBindGroup()
        {
            const u32 gen = m_viewRing.Generation();
            // Rebuild on a ring roll-over OR a change of the bound shadow map (pointer or generation -
            // a freed view's address can be reused, so the generation guards address aliasing).
            if (m_viewBg != nullptr && m_viewBgGen == gen && m_viewBgShadow == m_activeShadowView &&
                m_viewBgShadowGen == m_activeShadowGen)
            {
                return m_viewBg;
            }
            if (m_viewBg != nullptr)
            {
                // May sit in a submitted frame's descriptor set - retire (frame-aged) when possible.
                if (m_retire != nullptr)
                {
                    m_retire->Retire(m_viewBg);
                }
                else
                {
                    m_device->DestroyBindGroup(m_viewBg);
                }
                m_viewBg = nullptr;
            }
            if (m_viewRing.Buffer() == nullptr || m_activeShadowView == nullptr)
            {
                return nullptr;
            }
            rhi::BindGroupEntry entries[] = {
                rhi::BindGroupEntry::BufferEntry(m_viewRing.Buffer(), 0, kViewSlotSize), // b0
                rhi::BindGroupEntry::TextureEntry(m_activeShadowView),                   // t1 (CSM array)
                rhi::BindGroupEntry::SamplerEntry(m_shadowSampler),                      // s0 (compare)
            };
            rhi::BindGroupDesc bgd{};
            bgd.layout = m_viewLayout;
            bgd.entries = Span<const rhi::BindGroupEntry>{entries, 3};
            if (!m_device->CreateBindGroup(bgd, m_viewBg).IsOk())
            {
                m_viewBg = nullptr;
                return nullptr;
            }
            m_viewBgGen = gen;
            m_viewBgShadow = m_activeShadowView;
            m_viewBgShadowGen = m_activeShadowGen;
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
                    // The old group may sit in a submitted frame's descriptor bindings -
                    // retire (frame-aged) rather than destroy in place; direct destroy only
                    // when no queue is wired (Null-device tests).
                    if (m_retire != nullptr)
                    {
                        m_retire->Retire(found->bindGroup);
                    }
                    else
                    {
                        m_device->DestroyBindGroup(found->bindGroup);
                    }
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

        // Set 3 (splat material): splatmap + 4 albedos (white dummy for absent slots) + the two
        // samplers. Cached per splatmap view, validated by the uniqueId of ALL five views (never raw
        // pointers - address reuse); a hot-swap retires the stale group through the frame-retire queue.
        rhi::BindGroup* EnsureMaterialBindGroup(rhi::TextureView* splatmap,
                                                rhi::TextureView* const* albedos)
        {
            rhi::TextureView* sm = (splatmap != nullptr) ? splatmap : m_whiteView;
            rhi::TextureView* a[TerrainRenderData::kMaxLayers];
            for (u32 i = 0; i < TerrainRenderData::kMaxLayers; ++i)
            {
                a[i] = (albedos[i] != nullptr) ? albedos[i] : m_whiteView;
            }
            if (MaterialBindGroup* found = m_materialBindGroups.Find(sm))
            {
                bool match = found->splatId == sm->uniqueId;
                for (u32 i = 0; i < TerrainRenderData::kMaxLayers && match; ++i)
                {
                    match = found->albedoIds[i] == a[i]->uniqueId;
                }
                if (match)
                {
                    return found->bindGroup;
                }
                if (found->bindGroup != nullptr)
                {
                    if (m_retire != nullptr)
                    {
                        m_retire->Retire(found->bindGroup);
                    }
                    else
                    {
                        m_device->DestroyBindGroup(found->bindGroup);
                    }
                }
                m_materialBindGroups.Remove(sm);
            }
            rhi::BindGroupEntry entries[] = {
                rhi::BindGroupEntry::TextureEntry(sm),    rhi::BindGroupEntry::TextureEntry(a[0]),
                rhi::BindGroupEntry::TextureEntry(a[1]),  rhi::BindGroupEntry::TextureEntry(a[2]),
                rhi::BindGroupEntry::TextureEntry(a[3]),  rhi::BindGroupEntry::SamplerEntry(m_splatSampler),
                rhi::BindGroupEntry::SamplerEntry(m_albedoSampler),
            };
            rhi::BindGroupDesc bgd{};
            bgd.layout = m_materialLayout;
            bgd.entries = Span<const rhi::BindGroupEntry>{entries, 7};
            rhi::BindGroup* bg = nullptr;
            if (!m_device->CreateBindGroup(bgd, bg).IsOk())
            {
                return nullptr;
            }
            MaterialBindGroup entry{bg, sm->uniqueId, {a[0]->uniqueId, a[1]->uniqueId,
                                                       a[2]->uniqueId, a[3]->uniqueId}};
            m_materialBindGroups.InsertOrAssign(sm, entry);
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

            // Opaque terrain writes the forward GBUFFER: target 0 = shaded colour, 1 = view-space
            // normal, 2 = motion vector, 3 = material (roughness/metallic). The forward pass binds all
            // four, so the PSO must declare them (WebGPU rejects a target-count mismatch). No blend.
            rhi::ColorTargetState targets[4]{};
            targets[0].format = colorFormat;
            targets[1].format = render::kGNormalFormat;
            targets[2].format = render::kGVelocityFormat;
            targets[3].format = render::kGMaterialFormat;

            rhi::FragmentState frag{};
            frag.shader = rhi::ProgrammableStage{ps, u8"main", rhi::ShaderStage::Fragment};
            frag.targets = Span<const rhi::ColorTargetState>{targets, 4};

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

        // Depth-only PSO (vertex-only terrain_depth VS, 0 color targets). `biased` adds the shadow-pass
        // slope-scaled depth bias (the camera prepass must match the forward depth exactly, so no bias).
        rhi::RenderPipeline* EnsureDepthPipeline(rhi::TextureFormat depthFormat, bool biased)
        {
            DepthPso& p = m_depthPso[biased ? 1u : 0u];
            const u64 shaderVersion = m_shaders->Version(u8"terrain_depth");
            if (p.pso != nullptr && p.format == depthFormat && p.shaderVersion == shaderVersion)
            {
                return p.pso;
            }
            if (p.pso != nullptr)
            {
                m_device->DestroyRenderPipeline(p.pso);
                p.pso = nullptr;
            }
            rhi::ShaderModule* vs = m_shaders->GetVariant(
                u8"terrain_depth", shaders::ShaderStage::Vertex, shaders::ShaderFlags::None);
            if (vs == nullptr)
            {
                return nullptr;
            }
            const rhi::VertexAttribute attrs[] = {{rhi::VertexFormat::Float32x3, 0, 0}};
            rhi::VertexBufferLayout vbl{};
            vbl.stride = sizeof(Float3);
            vbl.stepMode = rhi::VertexStepMode::Vertex;
            vbl.attributes = Span<const rhi::VertexAttribute>{attrs, 1};

            rhi::DepthStencilState ds{};
            ds.format = depthFormat;
            ds.depthTestEnabled = true;
            ds.depthWriteEnabled = true;
            ds.depthCompare = rhi::CompareFunction::Less;
            if (biased)
            {
                ds.depthBias = 50;
                ds.depthBiasSlopeScale = 1.5f;
            }

            rhi::RenderPipelineDesc pd{};
            pd.layout = m_depthPipelineLayout; // 3 sets (no material) - depth VS samples none of it
            pd.vertex.shader = rhi::ProgrammableStage{vs, u8"main", rhi::ShaderStage::Vertex};
            pd.vertex.buffers = Span<const rhi::VertexBufferLayout>{&vbl, 1};
            // No fragment stage (Optional left empty) + no color targets = depth-only.
            pd.depthStencil = ds;
            pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
            pd.primitive.cullMode = rhi::CullMode::Back;
            pd.label = u8"terrain.depth";
            rhi::RenderPipeline* pso = nullptr;
            if (!m_device->CreateRenderPipeline(pd, pso).IsOk())
            {
                return nullptr;
            }
            p.pso = pso;
            p.format = depthFormat;
            p.shaderVersion = shaderVersion;
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
            for (auto& kv : m_materialBindGroups)
            {
                if (kv.value.bindGroup != nullptr)
                {
                    m_device->DestroyBindGroup(kv.value.bindGroup);
                }
            }
            m_materialBindGroups.Clear();
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
            for (DepthPso& dp : m_depthPso)
            {
                if (dp.pso != nullptr)
                {
                    m_device->DestroyRenderPipeline(dp.pso);
                    dp.pso = nullptr;
                }
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
            if (m_dummyShadowView != nullptr)
            {
                m_device->DestroyTextureView(m_dummyShadowView);
                m_dummyShadowView = nullptr;
            }
            if (m_dummyShadowTex != nullptr)
            {
                m_device->DestroyTexture(m_dummyShadowTex);
                m_dummyShadowTex = nullptr;
            }
            if (m_shadowSampler != nullptr)
            {
                m_device->DestroySampler(m_shadowSampler);
                m_shadowSampler = nullptr;
            }
            if (m_whiteView != nullptr)
            {
                m_device->DestroyTextureView(m_whiteView);
                m_whiteView = nullptr;
            }
            if (m_whiteTex != nullptr)
            {
                m_device->DestroyTexture(m_whiteTex);
                m_whiteTex = nullptr;
            }
            if (m_splatSampler != nullptr)
            {
                m_device->DestroySampler(m_splatSampler);
                m_splatSampler = nullptr;
            }
            if (m_albedoSampler != nullptr)
            {
                m_device->DestroySampler(m_albedoSampler);
                m_albedoSampler = nullptr;
            }
            if (m_depthPipelineLayout != nullptr)
            {
                m_device->DestroyPipelineLayout(m_depthPipelineLayout);
                m_depthPipelineLayout = nullptr;
            }
            if (m_pipelineLayout != nullptr)
            {
                m_device->DestroyPipelineLayout(m_pipelineLayout);
                m_pipelineLayout = nullptr;
            }
            if (m_materialLayout != nullptr)
            {
                m_device->DestroyBindGroupLayout(m_materialLayout);
                m_materialLayout = nullptr;
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

        struct MaterialBindGroup
        {
            rhi::BindGroup* bindGroup = nullptr;
            u64 splatId = 0;
            u64 albedoIds[TerrainRenderData::kMaxLayers] = {0, 0, 0, 0};
        };

        rhi::Device* m_device;
        shaders::ShaderSystem* m_shaders;
        render::DynamicUniformRing m_viewRing;
        render::DynamicUniformRing m_chunkRing;
        rhi::BindGroupLayout* m_viewLayout = nullptr;
        rhi::BindGroupLayout* m_chunkLayout = nullptr;
        rhi::BindGroupLayout* m_heightLayout = nullptr;
        rhi::BindGroupLayout* m_materialLayout = nullptr; // set 3: splat material
        rhi::PipelineLayout* m_pipelineLayout = nullptr;      // color: 4 sets
        rhi::PipelineLayout* m_depthPipelineLayout = nullptr; // depth: 3 sets (no material)
        rhi::Buffer* m_gridVertexBuffer = nullptr;
        u32 m_gridVertexCount = 0;
        LodMesh m_lodMeshes[tmodel::kMaxChunkLod + 1];
        rhi::BindGroup* m_viewBg = nullptr;
        u32 m_viewBgGen = 0;
        rhi::BindGroup* m_chunkBg = nullptr;
        u32 m_chunkBgGen = 0;
        HashMap<rhi::TextureView*, HeightBindGroup> m_heightBindGroups;
        // Splat material (set 3): white dummy + samplers + a per-splatmap cache.
        rhi::Sampler* m_splatSampler = nullptr;
        rhi::Sampler* m_albedoSampler = nullptr;
        rhi::Texture* m_whiteTex = nullptr;      // 1x1 white (absent splatmap/albedo slots)
        rhi::TextureView* m_whiteView = nullptr;
        HashMap<rhi::TextureView*, MaterialBindGroup> m_materialBindGroups;
        render::GpuRetireQueue* m_retire = nullptr; // borrowed (RenderSubsystem owns + ticks)
        // Shadow receive (set 0: t1 CSM array + s0 comparison sampler).
        rhi::Sampler* m_shadowSampler = nullptr;
        rhi::Texture* m_dummyShadowTex = nullptr;      // 1x1 Texture2DArray depth (no-caster fallback)
        rhi::TextureView* m_dummyShadowView = nullptr;
        rhi::TextureView* m_activeShadowView = nullptr; // borrowed: the CSM array, or the dummy
        bool m_dummyDepthInit = false; // the dummy depth transitioned out of UNDEFINED once (VUID-09600)
        u64 m_activeShadowGen = 0;
        rhi::TextureView* m_viewBgShadow = nullptr; // what the cached view BG was built against
        u64 m_viewBgShadowGen = 0;
        rhi::RenderPipeline* m_pso = nullptr;
        rhi::TextureFormat m_psoFormat = rhi::TextureFormat::Undefined;
        u64 m_psoShaderVersion = 0;
        DepthPso m_depthPso[2]; // [0] = prepass (no bias), [1] = shadow cascade (biased)
        rhi::TextureFormat m_depthFormat = rhi::TextureFormat::Undefined;
        Array<tmodel::ChunkDraw> m_draws; // scratch, reused each terrain (Resolve is single-threaded)
        u32 m_frameChunks = 0;      // color-pass visible chunks (the MaxChunksDrawn diagnostic)
        u32 m_maxChunksSeen = 0;
        u32 m_frameChunkAllocs = 0; // chunk-ring allocs this frame across ALL passes (sizes the ring)
        u32 m_maxChunkAllocs = 0;
        bool m_skirtsEnabled = true;
    };
}
