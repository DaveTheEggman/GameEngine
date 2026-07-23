/// Draconic::Render - the `:ibl` partition.
///
/// Image-Based Lighting: the split-sum environment pipeline (ported from Sedulous.Renderer/IBL with
/// improvements), PER SCENE. A frame can render several scenes side-by-side (editor pages), each
/// with its own authored sky - so the products live in per-scene CONTEXTS pooled by scene identity,
/// and every view binds ITS scene's products (the set-0 bind group is per-view downstream):
///   - env cubemap (256², RGBA16F)        : the source radiance, written from the scene's sky source
///                                          (procedural gradient / analytic / HDR equirect / cubemap).
///   - SH9 diffuse irradiance (buffer)    : 9 RGB spherical-harmonic coeffs projected from the env
///                                          cube (REPLACES Sedulous's 32² irradiance cube - cheaper,
///                                          smoother, seamless). Improvement over Sedulous.
///   - GGX prefiltered specular (cube+mips): Karis split-sum, importance-sampled per roughness mip.
/// Shared across scenes: the BRDF integration LUT (sky-independent), all pipelines/layouts/samplers,
/// and the PROGRAMMATIC equirect/cubemap pixel sources (SetEquirect/SetCubemap - tools/samples).
///
/// Precompute runs only when a context's source is dirty; products are persistent, imported every
/// frame so the forward pass orders after + samples them (set 0). Context generations come from ONE
/// system-wide counter, so a generation value never collides across contexts - downstream bind-group
/// caches can key on it alone ([[bind-group-cache-versioning]]).

module;
#include "Core/Prelude.h"

export module draconic.render:ibl;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;
import :data;        // SkySnapshot / SkyMode / ExtractedScene (context identity)
import :ibl_shaders; // IblFullscreenVS()/IblCommon()/... - HLSL source in IBLShaders.cppm

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render
{

    // Owns the per-scene IBL contexts + the passes that build their products. One per renderer.
    class IBLSystem
    {
    public:
        static constexpr u32 kEnvResolution = 256;
        static constexpr u32 kEnvMips = 5; // env mip pyramid (256..16) for prefilter PDF sampling
        static constexpr u32 kPrefilterRes = 256;
        static constexpr u32 kPrefilterMips = 5; // roughness = mip / (kPrefilterMips - 1)
        static constexpr u32 kBrdfResolution = 256;
        static constexpr u32 kShCoeffCount = 9;
        // A context unused for this many frames is evicted (its scene's page closed). Long enough
        // that nothing in flight can still reference the products.
        static constexpr u64 kEvictAfterFrames = 600;

        // ---- Per-scene context: products + sky state for ONE scene ---------------------------------
        class Context
        {
        public:
            // Products bound into the forward set 0 (per-view downstream).
            [[nodiscard]] rhi::TextureView* PrefilterView() const noexcept
            {
                return m_prefilterView;
            }
            [[nodiscard]] rhi::Buffer* ShBuffer() const noexcept { return m_shBuffer; }
            // Unique ACROSS contexts (one system-wide counter) - safe as a sole cache key.
            [[nodiscard]] u64 Generation() const noexcept { return m_generation; }
            // Stable per-context identity (creation-stamped, never reused) - downstream caches
            // that key on the env VIEW pair it with this (pointer reuse after eviction).
            [[nodiscard]] u64 Uid() const noexcept { return m_uid; }

            // This frame's graph handles (valid after Prepare). The forward ReadTexture/ReadBuffer's
            // these so the graph orders any precompute writes -> forward and barriers the products.
            [[nodiscard]] rendergraph::RGHandle PrefilterHandle() const noexcept
            {
                return m_prefilterH;
            }
            [[nodiscard]] rendergraph::RGHandle ShHandle() const noexcept { return m_shH; }
            // The full-radiance environment cube - sampled by the sky pass (background) at full detail.
            [[nodiscard]] rendergraph::RGHandle EnvHandle() const noexcept { return m_envH; }
            [[nodiscard]] rhi::TextureView* EnvView() const noexcept { return m_envSampleView; }
            [[nodiscard]] f32 SkyIntensity() const noexcept { return m_sky.intensity; }
            // Sun (from the scene's directional light) for the sky pass's crisp analytic disc.
            [[nodiscard]] Float3 SunDir() const noexcept { return m_sunDir; }
            [[nodiscard]] f32 SunIntensity() const noexcept { return m_sky.sunIntensity; }
            [[nodiscard]] f32 SunAngularSize() const noexcept { return m_sky.sunAngularSize; }
            // The sky pass draws a crisp analytic sun disc for the untextured skies (procedural +
            // Preetham); textured envs (HDR/cubemap) carry their own sun, so it's suppressed there.
            [[nodiscard]] bool HasSunDisc() const noexcept
            {
                return m_sky.mode != SkyMode::HDREquirect && m_sky.mode != SkyMode::Cubemap;
            }

        private:
            friend class IBLSystem;
            const void* m_scene = nullptr; // identity key (the scene's ExtractedScene)
            u64 m_uid = 0;
            u64 m_lastUsedFrame = 0;

            rhi::Texture* m_envCube = nullptr;
            rhi::TextureView* m_envSampleView = nullptr;
            rhi::TextureView* m_envMipView[kEnvMips] = {};
            rhi::Texture* m_prefilterCube = nullptr;
            rhi::TextureView* m_prefilterView = nullptr;
            rhi::Buffer* m_shBuffer = nullptr;
            rhi::BindGroup* m_envBindGroup = nullptr;  // env full-chain sample (prefilter input)
            rhi::BindGroup* m_envMipBG[kEnvMips] = {}; // mip m as the downsample source
            rhi::BindGroup* m_shBindGroup = nullptr;   // env + SH output buffer (compute)
            // Asset-driven sky texture (external product view - NOT owned): bind groups only.
            rhi::BindGroup* m_externalEquirectBG = nullptr;
            rhi::BindGroup* m_externalCubeBG = nullptr;
            u64 m_externalUid = 0;

            rhi::ResourceState m_envState = rhi::ResourceState::Undefined;
            rhi::ResourceState m_prefilterState = rhi::ResourceState::Undefined;
            rendergraph::RGHandle m_prefilterH = {};
            rendergraph::RGHandle m_shH = {};
            rendergraph::RGHandle m_envH = {};

            SkySnapshot m_sky{};
            Float3 m_sunDir = Float3{0.0f, -1.0f, 0.0f};
            bool m_dirty = true;
            u64 m_generation = 0;
            u64 m_sourceStamp = 0; // programmatic SetEquirect/SetCubemap change tick
        };

        IBLSystem(rhi::Device& device, shaders::ShaderSystem& shaders) noexcept
            : m_device(&device), m_shaders(&shaders)
        {
        }
        ~IBLSystem() { Shutdown(); }
        IBLSystem(const IBLSystem&) = delete;
        IBLSystem& operator=(const IBLSystem&) = delete;

        Status Initialize()
        {
            m_shaders->RegisterSource(u8"ibl_fs", shaders::ShaderStage::Vertex, IblFullscreenVS());
            // Cube/LUT fragment shaders share the fullscreen VS; the cube ones prepend IblCommon().
            m_shaders->RegisterSource(u8"ibl_procenv", shaders::ShaderStage::Fragment,
                                      Concat(IblCommon(), IblProcEnvPS()));
            m_shaders->RegisterSource(u8"ibl_analytic", shaders::ShaderStage::Fragment,
                                      Concat(IblCommon(), IblAnalyticPS()));
            m_shaders->RegisterSource(u8"ibl_equirect", shaders::ShaderStage::Fragment,
                                      Concat(IblCommon(), IblEquirectPS()));
            m_shaders->RegisterSource(u8"ibl_cubemap", shaders::ShaderStage::Fragment,
                                      Concat(IblCommon(), IblCubemapPS()));
            m_shaders->RegisterSource(u8"ibl_downsample", shaders::ShaderStage::Fragment,
                                      Concat(IblCommon(), IblDownsamplePS()));
            m_shaders->RegisterSource(u8"ibl_prefilter", shaders::ShaderStage::Fragment,
                                      Concat(IblCommon(), IblPrefilterPS()));
            m_shaders->RegisterSource(u8"ibl_brdf", shaders::ShaderStage::Fragment, IblBrdfPS());
            m_shaders->RegisterSource(u8"ibl_sh", shaders::ShaderStage::Compute, IblShProjectCS());

            if (!CreateSharedResources())
            {
                return Status{ErrorCode::Unknown};
            }
            if (!CreatePipelines())
            {
                return Status{ErrorCode::Unknown};
            }
            return Status{};
        }

        // Shared products (sky-independent).
        [[nodiscard]] rhi::TextureView* BrdfView() const noexcept { return m_brdfView; }
        [[nodiscard]] rendergraph::RGHandle BrdfHandle() const noexcept { return m_brdfH; }
        [[nodiscard]] u64 ShBytes() const noexcept { return sizeof(f32) * 4 * kShCoeffCount; }
        [[nodiscard]] f32 MaxLod() const noexcept { return static_cast<f32>(kPrefilterMips - 1); }
        [[nodiscard]] bool Ready() const noexcept { return m_ready; }
        [[nodiscard]] usize ContextCount() const noexcept { return m_contexts.Size(); }

        // Frame tick: import the shared BRDF into this frame's graph (declare its one-time build),
        // advance the LRU clock, and evict contexts whose scene hasn't rendered in a long time.
        void BeginFrame(rendergraph::RenderGraph& graph)
        {
            if (!m_ready)
            {
                return;
            }
            ++m_frame;
            m_brdfH = graph.ImportTarget(u8"ibl.brdf", m_brdfLut, m_brdfView,
                                         rhi::ResourceState::ShaderRead, m_brdfState);
            m_brdfState = rhi::ResourceState::ShaderRead;
            if (!m_brdfDone)
            {
                DeclareBrdf(graph, m_brdfH);
                m_brdfDone = true;
            }

            for (usize i = 0; i < m_contexts.Size(); /**/)
            {
                if (m_frame - m_contexts[i]->m_lastUsedFrame > kEvictAfterFrames)
                {
                    DestroyContext(*m_contexts[i]);
                    m_contexts.RemoveAtSwap(i);
                }
                else
                {
                    ++i;
                }
            }
        }

        // Get-or-create the SCENE's context, apply its authored sky + sun, import its products into
        // this frame's graph, and declare the precompute passes when dirty. Null when unavailable.
        Context* Prepare(const void* scene, const SkySnapshot& sky, const Float3& sunDir,
                         rendergraph::RenderGraph& graph)
        {
            if (!m_ready)
            {
                return nullptr;
            }
            Context* ctx = nullptr;
            for (const UniquePtr<Context>& c : m_contexts)
            {
                if (c->m_scene == scene)
                {
                    ctx = c.Get();
                    break;
                }
            }
            if (ctx == nullptr)
            {
                UniquePtr<Context> fresh = MakeUnique<Context>(DefaultAllocator());
                fresh->m_scene = scene;
                fresh->m_uid = ++m_nextContextUid;
                if (!CreateContextResources(*fresh))
                {
                    DestroyContext(*fresh);
                    return nullptr;
                }
                m_contexts.PushBack(Move(fresh));
                ctx = m_contexts[m_contexts.Size() - 1].Get();
            }
            ctx->m_lastUsedFrame = m_frame;

            // Sky authoring: re-dirty only for fields baked into the env cube (sunAngularSize is
            // analytic-only - the sky pass draws the disc live). Sun direction feeds the procedural env.
            if (!PrecomputeEqual(sky, ctx->m_sky))
            {
                ctx->m_dirty = true;
            }
            if (sunDir.x != ctx->m_sunDir.x || sunDir.y != ctx->m_sunDir.y ||
                sunDir.z != ctx->m_sunDir.z)
            {
                ctx->m_sunDir = sunDir;
                ctx->m_dirty = true;
            }
            // A programmatic pixel source changed (SetEquirect/SetCubemap): textured modes re-bake.
            if (ctx->m_sourceStamp != m_sourceStamp &&
                (sky.mode == SkyMode::HDREquirect || sky.mode == SkyMode::Cubemap))
            {
                ctx->m_dirty = true;
            }
            ctx->m_sourceStamp = m_sourceStamp;
            // Asset-driven sky texture: (re)build the context's external bind group when the PRODUCT
            // changes - detected by uid, never the pointer (reloads reuse freed addresses; deferred
            // product destruction keeps the old view alive for in-flight frames).
            if (sky.textureUid != ctx->m_externalUid)
            {
                DestroyExternalBindGroups(*ctx);
                ctx->m_externalUid = sky.textureUid;
                if (sky.texture != nullptr)
                {
                    if (sky.textureIsCube)
                    {
                        if (EnsureCubemapPipeline())
                        {
                            rhi::BindGroupEntry be[] = {
                                rhi::BindGroupEntry::TextureEntry(sky.texture),
                                rhi::BindGroupEntry::SamplerEntry(m_sampler)};
                            rhi::BindGroupDesc bgd{};
                            bgd.layout = m_envLayout;
                            bgd.entries = Span<const rhi::BindGroupEntry>{be, 2};
                            if (!m_device->CreateBindGroup(bgd, ctx->m_externalCubeBG).IsOk())
                            {
                                ctx->m_externalCubeBG = nullptr;
                            }
                        }
                    }
                    else
                    {
                        if (EnsureEquirectPipeline())
                        {
                            rhi::BindGroupEntry be[] = {
                                rhi::BindGroupEntry::TextureEntry(sky.texture),
                                rhi::BindGroupEntry::SamplerEntry(m_equirectSampler)};
                            rhi::BindGroupDesc bgd{};
                            bgd.layout = m_equirectLayout;
                            bgd.entries = Span<const rhi::BindGroupEntry>{be, 2};
                            if (!m_device->CreateBindGroup(bgd, ctx->m_externalEquirectBG).IsOk())
                            {
                                ctx->m_externalEquirectBG = nullptr;
                            }
                        }
                    }
                }
                ctx->m_dirty = true;
            }
            ctx->m_sky =
                sky; // always store the latest (the sky pass reads sun size/intensity live)

            ProcessContext(*ctx, graph);
            return ctx;
        }

        // Set the shared PROGRAMMATIC HDR equirectangular source (RGBA32F, w*h*4 floats) - the
        // tools/samples pixel path; a scene's ASSET sky texture takes precedence per context.
        void SetEquirect(u32 w, u32 h, Span<const f32> rgba)
        {
            if (!m_ready || w == 0 || h == 0 || rgba.Size() < static_cast<usize>(w) * h * 4u)
            {
                return;
            }
            DestroyEquirect();
            rhi::TextureDesc td{};
            td.format = rhi::TextureFormat::RGBA32Float;
            td.width = w;
            td.height = h;
            td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
            td.label = u8"ibl.equirect";
            if (!m_device->CreateTexture(td, m_equirectTex).IsOk())
            {
                m_equirectTex = nullptr;
                return;
            }
            rhi::TextureViewDesc vd{};
            vd.format = rhi::TextureFormat::RGBA32Float;
            vd.dimension = rhi::TextureViewDimension::Texture2D;
            if (!m_device->CreateTextureView(m_equirectTex, vd, m_equirectView).IsOk())
            {
                DestroyEquirect();
                return;
            }
            const u64 bytes = static_cast<u64>(w) * h * 4u * sizeof(f32);
            rhi::BufferDesc sd{};
            sd.size = bytes;
            sd.usage = rhi::BufferUsage::CopySrc;
            sd.memory = rhi::MemoryLocation::CpuToGpu;
            sd.label = u8"ibl.equirectStaging";
            if (!m_device->CreateBuffer(sd, m_equirectStaging).IsOk())
            {
                DestroyEquirect();
                return;
            }
            if (void* p = m_equirectStaging->Map())
            {
                MemCopy(p, rgba.Data(), bytes);
                m_equirectStaging->Unmap();
            }
            if (!EnsureEquirectPipeline())
            {
                DestroyEquirect();
                return;
            }
            rhi::BindGroupEntry be[] = {rhi::BindGroupEntry::TextureEntry(m_equirectView),
                                        rhi::BindGroupEntry::SamplerEntry(m_equirectSampler)};
            rhi::BindGroupDesc bgd{};
            bgd.layout = m_equirectLayout;
            bgd.entries = Span<const rhi::BindGroupEntry>{be, 2};
            if (!m_device->CreateBindGroup(bgd, m_equirectBindGroup).IsOk())
            {
                m_equirectBindGroup = nullptr;
                DestroyEquirect();
                return;
            }
            m_equirectW = w;
            m_equirectH = h;
            m_equirectPending = true;
            ++m_sourceStamp;
        }

        // Set the shared PROGRAMMATIC cubemap source: 6 RGBA8 faces (+X,-X,+Y,-Y,+Z,-Z) concatenated,
        // each faceSize*faceSize*4 bytes. Same precedence note as SetEquirect.
        void SetCubemap(u32 faceSize, Span<const u8> sixFaces)
        {
            const u64 faceBytes = static_cast<u64>(faceSize) * faceSize * 4u;
            if (!m_ready || faceSize == 0 || sixFaces.Size() < faceBytes * 6u)
            {
                return;
            }
            DestroyCubemap();
            // sRGB format so the hardware decodes the (sRGB-encoded LDR) faces to linear on sample - the
            // env cube is a linear working-space texture. Without this the sky reads washed out.
            rhi::TextureDesc td{};
            td.format = rhi::TextureFormat::RGBA8UnormSrgb;
            td.width = faceSize;
            td.height = faceSize;
            td.arrayLayerCount = 6;
            td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
            td.label = u8"ibl.srcCube";
            if (!m_device->CreateTexture(td, m_srcCube).IsOk())
            {
                m_srcCube = nullptr;
                return;
            }
            rhi::TextureViewDesc vd{};
            vd.format = rhi::TextureFormat::RGBA8UnormSrgb;
            vd.dimension = rhi::TextureViewDimension::TextureCube;
            vd.arrayLayerCount = 6;
            if (!m_device->CreateTextureView(m_srcCube, vd, m_srcCubeView).IsOk())
            {
                DestroyCubemap();
                return;
            }
            rhi::BufferDesc sd{};
            sd.size = faceBytes * 6u;
            sd.usage = rhi::BufferUsage::CopySrc;
            sd.memory = rhi::MemoryLocation::CpuToGpu;
            sd.label = u8"ibl.srcCubeStaging";
            if (!m_device->CreateBuffer(sd, m_cubemapStaging).IsOk())
            {
                DestroyCubemap();
                return;
            }
            if (void* p = m_cubemapStaging->Map())
            {
                MemCopy(p, sixFaces.Data(), faceBytes * 6u);
                m_cubemapStaging->Unmap();
            }
            if (!EnsureCubemapPipeline())
            {
                DestroyCubemap();
                return;
            }
            rhi::BindGroupEntry be[] = {rhi::BindGroupEntry::TextureEntry(m_srcCubeView),
                                        rhi::BindGroupEntry::SamplerEntry(m_sampler)};
            rhi::BindGroupDesc bgd{};
            bgd.layout = m_envLayout;
            bgd.entries = Span<const rhi::BindGroupEntry>{be, 2};
            if (!m_device->CreateBindGroup(bgd, m_cubemapBindGroup).IsOk())
            {
                m_cubemapBindGroup = nullptr;
                DestroyCubemap();
                return;
            }
            m_cubemapFaceSize = faceSize;
            m_cubemapPending = true;
            ++m_sourceStamp;
        }

        // Pending texture uploads (equirect/cubemap staging -> texture) on the frame's encoder, BEFORE the
        // graph executes - so the env-build passes sample an already-uploaded, shader-readable source.
        void Upload(rhi::CommandEncoder& enc)
        {
            if (m_equirectPending && m_equirectTex != nullptr && m_equirectStaging != nullptr)
            {
                enc.TransitionTexture(m_equirectTex, rhi::ResourceState::Undefined,
                                      rhi::ResourceState::CopyDst);
                rhi::BufferTextureCopyRegion r{};
                r.bytesPerRow = m_equirectW * 4u * static_cast<u32>(sizeof(f32));
                r.rowsPerImage = m_equirectH;
                r.textureExtent = rhi::Extent3D{m_equirectW, m_equirectH, 1};
                enc.CopyBufferToTexture(m_equirectStaging, m_equirectTex, r);
                enc.TransitionTexture(m_equirectTex, rhi::ResourceState::CopyDst,
                                      rhi::ResourceState::ShaderRead);
                m_equirectPending = false;
            }
            if (m_cubemapPending && m_srcCube != nullptr && m_cubemapStaging != nullptr)
            {
                enc.TransitionTexture(m_srcCube, rhi::ResourceState::Undefined,
                                      rhi::ResourceState::CopyDst);
                const u64 faceBytes = static_cast<u64>(m_cubemapFaceSize) * m_cubemapFaceSize * 4u;
                for (u32 f = 0; f < 6; ++f)
                {
                    rhi::BufferTextureCopyRegion r{};
                    r.bufferOffset = faceBytes * f;
                    r.bytesPerRow = m_cubemapFaceSize * 4u;
                    r.rowsPerImage = m_cubemapFaceSize;
                    r.textureArrayLayer = f;
                    r.textureExtent = rhi::Extent3D{m_cubemapFaceSize, m_cubemapFaceSize, 1};
                    enc.CopyBufferToTexture(m_cubemapStaging, m_srcCube, r);
                }
                enc.TransitionTexture(m_srcCube, rhi::ResourceState::CopyDst,
                                      rhi::ResourceState::ShaderRead);
                m_cubemapPending = false;
            }
        }

    private:
        // Declare one context's precompute into this frame's graph. Products are imported EVERY frame
        // (so the forward can read this frame's handles); the write passes only run when dirty.
        void ProcessContext(Context& ctx, rendergraph::RenderGraph& graph)
        {
            ctx.m_prefilterH =
                graph.ImportTarget(u8"ibl.prefilter", ctx.m_prefilterCube, ctx.m_prefilterView,
                                   rhi::ResourceState::ShaderRead, ctx.m_prefilterState);
            ctx.m_prefilterState = rhi::ResourceState::ShaderRead;
            ctx.m_shH = graph.ImportBuffer(u8"ibl.sh", ctx.m_shBuffer);
            // The env cube is imported every frame too (the sky pass reads it for the visible background).
            ctx.m_envH = graph.ImportTarget(u8"ibl.env", ctx.m_envCube, ctx.m_envSampleView,
                                            rhi::ResourceState::ShaderRead, ctx.m_envState);
            ctx.m_envState = rhi::ResourceState::ShaderRead;

            if (!ctx.m_dirty)
            {
                return;
            }
            ctx.m_dirty = false;
            ctx.m_generation = ++m_nextGeneration; // system-wide: never collides across contexts

            // (1) Source -> env cube: 6 faces. The scene-authored ASSET texture wins over the
            // programmatic pixel path (SetEquirect/SetCubemap) when both are present.
            const rendergraph::RGHandle envH = ctx.m_envH;
            rhi::BindGroup* equirectBG = (ctx.m_externalEquirectBG != nullptr)
                                             ? ctx.m_externalEquirectBG
                                             : m_equirectBindGroup;
            rhi::BindGroup* cubemapBG =
                (ctx.m_externalCubeBG != nullptr) ? ctx.m_externalCubeBG : m_cubemapBindGroup;
            const bool useEquirect =
                (ctx.m_sky.mode == SkyMode::HDREquirect) && equirectBG != nullptr;
            const bool useCubemap = (ctx.m_sky.mode == SkyMode::Cubemap) && cubemapBG != nullptr;
            const bool useAnalytic = (ctx.m_sky.mode == SkyMode::Analytic);
            rhi::RenderPipeline* envPipe = useEquirect   ? m_equirectPipeline
                                           : useCubemap  ? m_cubemapPipeline
                                           : useAnalytic ? m_analyticPipeline
                                                         : m_envPipeline;
            rhi::BindGroup* envBG = useEquirect ? equirectBG : useCubemap ? cubemapBG : nullptr;
            for (u32 face = 0; face < 6; ++face)
            {
                IblPush push = MakeSkyPush(ctx, static_cast<i32>(face));
                graph.AddRenderPass(u8"ibl.env.face",
                                    [envH, face, push, envPipe, envBG](rendergraph::PassBuilder& b)
                                    {
                                        b.SetColorTarget(
                                            0, envH, rhi::LoadOp::Clear, rhi::StoreOp::Store,
                                            rhi::ClearColor::Black(),
                                            rendergraph::RGSubresourceRange{0, 1, face, 1});
                                        b.SetViewport(0, 0, kEnvResolution, kEnvResolution);
                                        b.NeverCull();
                                        b.SetExecute(
                                            [push, envPipe, envBG](rhi::RenderPassEncoder& rp)
                                            {
                                                rp.SetPipeline(envPipe);
                                                if (envBG != nullptr)
                                                {
                                                    rp.SetBindGroup(0, envBG, Span<const u32>{});
                                                }
                                                rp.SetPushConstants(rhi::ShaderStage::Fragment, 0,
                                                                    sizeof(IblPush), &push);
                                                rp.Draw(3, 1, 0, 0);
                                            });
                                    });
            }

            // (2) Build the env mip pyramid: box-downsample each mip from the previous. Reads mip m-1 (a
            // single-mip view) and writes mip m - non-overlapping subresources, so the graph orders + barriers
            // it correctly. SH/prefilter (whole-resource reads) then run after the whole chain is written.
            DeclareEnvMips(ctx, graph, envH);

            // (3) env -> SH9 diffuse (compute), (4) env -> prefilter mips (PDF-samples the pyramid).
            DeclareShProjection(ctx, graph, envH, ctx.m_shH);
            DeclarePrefilter(ctx, graph, envH, ctx.m_prefilterH);
        }

        struct IblPush
        {
            i32 faceIndex = 0;
            i32 mode = 0;
            f32 roughness = 0.0f;
            f32 skyIntensity = 1.0f;
            Float4 sun{};     // xyz = direction, w = sun angular size (deg)
            Float4 horizon{}; // rgb, a = sun intensity
            Float4 zenith{};  // rgb, a = rotation (radians)
            Float4 ground{};  // rgb
        };

        // Build the procedural-env push for one cube face from the context's sky + sun direction.
        [[nodiscard]] static IblPush MakeSkyPush(const Context& ctx, i32 face)
        {
            IblPush p{};
            p.faceIndex = face;
            p.mode = static_cast<i32>(ctx.m_sky.mode);
            p.skyIntensity = ctx.m_sky.intensity;
            p.sun =
                Float4{ctx.m_sunDir.x, ctx.m_sunDir.y, ctx.m_sunDir.z, ctx.m_sky.sunAngularSize};
            p.horizon = Float4{ctx.m_sky.horizon.x, ctx.m_sky.horizon.y, ctx.m_sky.horizon.z,
                               ctx.m_sky.sunIntensity};
            p.zenith = Float4{ctx.m_sky.zenith.x, ctx.m_sky.zenith.y, ctx.m_sky.zenith.z,
                              ctx.m_sky.rotation};
            p.ground = Float4{ctx.m_sky.ground.x, ctx.m_sky.ground.y, ctx.m_sky.ground.z,
                              ctx.m_sky.turbidity};
            return p;
        }

        void DestroyExternalBindGroups(Context& ctx)
        {
            if (ctx.m_externalEquirectBG)
            {
                m_device->DestroyBindGroup(ctx.m_externalEquirectBG);
                ctx.m_externalEquirectBG = nullptr;
            }
            if (ctx.m_externalCubeBG)
            {
                m_device->DestroyBindGroup(ctx.m_externalCubeBG);
                ctx.m_externalCubeBG = nullptr;
            }
        }

        // Equal w.r.t. the fields baked into the env cube (drives the precompute-rebuild decision).
        // sunAngularSize is EXCLUDED - it only affects the analytic sky-pass sun, not the cube.
        [[nodiscard]] static bool PrecomputeEqual(const SkySnapshot& a, const SkySnapshot& b)
        {
            return a.mode == b.mode && a.intensity == b.intensity && a.rotation == b.rotation &&
                   a.textureUid == b.textureUid && a.horizon.x == b.horizon.x &&
                   a.horizon.y == b.horizon.y && a.horizon.z == b.horizon.z &&
                   a.zenith.x == b.zenith.x && a.zenith.y == b.zenith.y &&
                   a.zenith.z == b.zenith.z && a.ground.x == b.ground.x &&
                   a.ground.y == b.ground.y && a.ground.z == b.ground.z &&
                   a.sunIntensity == b.sunIntensity && a.turbidity == b.turbidity;
        }

        void DeclareShProjection(Context& ctx, rendergraph::RenderGraph& graph,
                                 rendergraph::RGHandle envH, rendergraph::RGHandle shH)
        {
            rhi::BindGroup* shBG = ctx.m_shBindGroup;
            graph.AddComputePass(u8"ibl.sh",
                                 [this, envH, shH, shBG](rendergraph::PassBuilder& b)
                                 {
                                     b.ReadTexture(envH);
                                     b.WriteStorage(shH);
                                     b.SetComputeExecute(
                                         [this, shBG](rhi::ComputePassEncoder& cp)
                                         {
                                             if (shBG == nullptr)
                                             {
                                                 return;
                                             }
                                             cp.SetPipeline(m_shPipeline);
                                             cp.SetBindGroup(0, shBG, Span<const u32>{});
                                             cp.Dispatch(1, 1, 1);
                                         });
                                 });
        }

        // Box-downsample the env cube's mip pyramid: mip m from mip m-1 (per face). Each pass reads only the
        // finer mip (a single-mip source view/bind-group) and renders the coarser one, so read + write never
        // touch the same subresource; the graph's per-subresource barriers serialize the chain by mip.
        void DeclareEnvMips(Context& ctx, rendergraph::RenderGraph& graph,
                            rendergraph::RGHandle envH)
        {
            for (u32 mip = 1; mip < kEnvMips; ++mip)
            {
                const u32 res = kEnvResolution >> mip;
                rhi::BindGroup* srcBG = ctx.m_envMipBG[mip - 1];
                for (u32 face = 0; face < 6; ++face)
                {
                    IblPush push{};
                    push.faceIndex = static_cast<i32>(face);
                    graph.AddRenderPass(
                        u8"ibl.env.mip",
                        [this, envH, mip, face, res, push, srcBG](rendergraph::PassBuilder& b)
                        {
                            b.ReadTexture(envH, rendergraph::RGSubresourceRange{mip - 1, 1, 0, 6});
                            b.SetColorTarget(0, envH, rhi::LoadOp::Clear, rhi::StoreOp::Store,
                                             rhi::ClearColor::Black(),
                                             rendergraph::RGSubresourceRange{mip, 1, face, 1});
                            b.SetViewport(0, 0, res, res);
                            b.NeverCull();
                            b.SetExecute(
                                [this, push, srcBG](rhi::RenderPassEncoder& rp)
                                {
                                    rp.SetPipeline(m_downsamplePipeline);
                                    rp.SetBindGroup(0, srcBG, Span<const u32>{});
                                    rp.SetPushConstants(rhi::ShaderStage::Fragment, 0,
                                                        sizeof(IblPush), &push);
                                    rp.Draw(3, 1, 0, 0);
                                });
                        });
                }
            }
        }

        void DeclarePrefilter(Context& ctx, rendergraph::RenderGraph& graph,
                              rendergraph::RGHandle envH, rendergraph::RGHandle preH)
        {
            rhi::BindGroup* envBG = ctx.m_envBindGroup;
            for (u32 mip = 0; mip < kPrefilterMips; ++mip)
            {
                const u32 res = kPrefilterRes >> mip;
                const f32 roughness =
                    (kPrefilterMips > 1)
                        ? static_cast<f32>(mip) / static_cast<f32>(kPrefilterMips - 1)
                        : 0.0f;
                for (u32 face = 0; face < 6; ++face)
                {
                    IblPush push{};
                    push.faceIndex = static_cast<i32>(face);
                    push.roughness = roughness;
                    graph.AddRenderPass(
                        u8"ibl.prefilter",
                        [this, envH, preH, mip, face, res, push, envBG](rendergraph::PassBuilder& b)
                        {
                            b.ReadTexture(envH);
                            b.SetColorTarget(0, preH, rhi::LoadOp::Clear, rhi::StoreOp::Store,
                                             rhi::ClearColor::Black(),
                                             rendergraph::RGSubresourceRange{mip, 1, face, 1});
                            b.SetViewport(0, 0, res, res);
                            b.NeverCull();
                            b.SetExecute(
                                [this, push, envBG](rhi::RenderPassEncoder& rp)
                                {
                                    rp.SetPipeline(m_prefilterPipeline);
                                    rp.SetBindGroup(0, envBG, Span<const u32>{});
                                    rp.SetPushConstants(rhi::ShaderStage::Fragment, 0,
                                                        sizeof(IblPush), &push);
                                    rp.Draw(3, 1, 0, 0);
                                });
                        });
                }
            }
        }

        void DeclareBrdf(rendergraph::RenderGraph& graph, rendergraph::RGHandle brdfH)
        {
            graph.AddRenderPass(u8"ibl.brdf",
                                [this, brdfH](rendergraph::PassBuilder& b)
                                {
                                    b.SetColorTarget(0, brdfH, rhi::LoadOp::Clear,
                                                     rhi::StoreOp::Store, rhi::ClearColor::Black());
                                    b.SetViewport(0, 0, kBrdfResolution, kBrdfResolution);
                                    b.NeverCull();
                                    b.SetExecute(
                                        [this](rhi::RenderPassEncoder& rp)
                                        {
                                            rp.SetPipeline(m_brdfPipeline);
                                            rp.Draw(3, 1, 0, 0);
                                        });
                                });
        }

        static constexpr rhi::TextureFormat kCubeFormat = rhi::TextureFormat::RGBA16Float;
        static constexpr rhi::TextureFormat kBrdfFormat = rhi::TextureFormat::RG16Float;

        // Shared, sky-independent: the BRDF LUT + the env/cube sampler.
        bool CreateSharedResources()
        {
            rhi::TextureDesc bd{};
            bd.format = kBrdfFormat;
            bd.width = kBrdfResolution;
            bd.height = kBrdfResolution;
            bd.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled;
            bd.label = u8"ibl.brdf";
            if (!m_device->CreateTexture(bd, m_brdfLut).IsOk())
            {
                return false;
            }
            rhi::TextureViewDesc bv{};
            bv.format = kBrdfFormat;
            bv.dimension = rhi::TextureViewDimension::Texture2D;
            if (!m_device->CreateTextureView(m_brdfLut, bv, m_brdfView).IsOk())
            {
                return false;
            }

            rhi::SamplerDesc ss{};
            ss.minFilter = rhi::FilterMode::Linear;
            ss.magFilter = rhi::FilterMode::Linear;
            ss.mipmapFilter = rhi::MipmapFilterMode::Linear;
            ss.addressU = rhi::AddressMode::ClampToEdge;
            ss.addressV = rhi::AddressMode::ClampToEdge;
            ss.addressW = rhi::AddressMode::ClampToEdge;
            ss.label = u8"ibl.sampler";
            if (!m_device->CreateSampler(ss, m_sampler).IsOk())
            {
                return false;
            }
            return true;
        }

        // One scene's products + the bind groups referencing them.
        bool CreateContextResources(Context& ctx)
        {
            // Env cube (mip pyramid): mip 0 holds the full-res source radiance; mips 1..N are box-downsampled
            // so the prefilter can PDF-sample a pre-averaged mip per GGX sample (firefly suppression).
            rhi::TextureDesc ed{};
            ed.format = kCubeFormat;
            ed.width = kEnvResolution;
            ed.height = kEnvResolution;
            ed.arrayLayerCount = 6;
            ed.mipLevelCount = kEnvMips;
            ed.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled;
            ed.label = u8"ibl.env";
            if (!m_device->CreateTexture(ed, ctx.m_envCube).IsOk())
            {
                return false;
            }
            rhi::TextureViewDesc ev{};
            ev.format = kCubeFormat;
            ev.dimension = rhi::TextureViewDimension::TextureCube;
            ev.arrayLayerCount = 6;
            ev.mipLevelCount = kEnvMips;
            if (!m_device->CreateTextureView(ctx.m_envCube, ev, ctx.m_envSampleView).IsOk())
            {
                return false;
            }
            // Single-mip cube views of each env mip - bound as the source when downsampling the NEXT mip, so
            // the read descriptor covers only mip m (never the mip m+1 being rendered -> no read/write hazard).
            for (u32 m = 0; m < kEnvMips; ++m)
            {
                rhi::TextureViewDesc mv{};
                mv.format = kCubeFormat;
                mv.dimension = rhi::TextureViewDimension::TextureCube;
                mv.baseMipLevel = m;
                mv.mipLevelCount = 1;
                mv.arrayLayerCount = 6;
                if (!m_device->CreateTextureView(ctx.m_envCube, mv, ctx.m_envMipView[m]).IsOk())
                {
                    return false;
                }
            }

            // Prefilter cube (mip chain).
            rhi::TextureDesc pd{};
            pd.format = kCubeFormat;
            pd.width = kPrefilterRes;
            pd.height = kPrefilterRes;
            pd.arrayLayerCount = 6;
            pd.mipLevelCount = kPrefilterMips;
            pd.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled;
            pd.label = u8"ibl.prefilter";
            if (!m_device->CreateTexture(pd, ctx.m_prefilterCube).IsOk())
            {
                return false;
            }
            rhi::TextureViewDesc pv{};
            pv.format = kCubeFormat;
            pv.dimension = rhi::TextureViewDimension::TextureCube;
            pv.arrayLayerCount = 6;
            pv.mipLevelCount = kPrefilterMips;
            if (!m_device->CreateTextureView(ctx.m_prefilterCube, pv, ctx.m_prefilterView).IsOk())
            {
                return false;
            }

            // SH9 coefficient buffer (RW for the compute write, read-only in forward).
            rhi::BufferDesc sd{};
            sd.size = ShBytes();
            sd.usage = rhi::BufferUsage::Storage;
            sd.memory = rhi::MemoryLocation::GpuOnly;
            sd.label = u8"ibl.sh";
            if (!m_device->CreateBuffer(sd, ctx.m_shBuffer).IsOk())
            {
                return false;
            }

            // env sample bind group (for prefilter: full mip chain) + per-mip downsample sources.
            rhi::BindGroupEntry be[] = {rhi::BindGroupEntry::TextureEntry(ctx.m_envSampleView),
                                        rhi::BindGroupEntry::SamplerEntry(m_sampler)};
            rhi::BindGroupDesc bgd{};
            bgd.layout = m_envLayout;
            bgd.entries = Span<const rhi::BindGroupEntry>{be, 2};
            if (!m_device->CreateBindGroup(bgd, ctx.m_envBindGroup).IsOk())
            {
                return false;
            }
            for (u32 m = 0; m < kEnvMips; ++m)
            {
                rhi::BindGroupEntry me[] = {rhi::BindGroupEntry::TextureEntry(ctx.m_envMipView[m]),
                                            rhi::BindGroupEntry::SamplerEntry(m_sampler)};
                rhi::BindGroupDesc md{};
                md.layout = m_envLayout;
                md.entries = Span<const rhi::BindGroupEntry>{me, 2};
                if (!m_device->CreateBindGroup(md, ctx.m_envMipBG[m]).IsOk())
                {
                    return false;
                }
            }

            // SH compute bind group (t0 env cube + s0 sampler + u0 SH buffer).
            rhi::BindGroupEntry she[] = {
                rhi::BindGroupEntry::TextureEntry(ctx.m_envSampleView),
                rhi::BindGroupEntry::SamplerEntry(m_sampler),
                rhi::BindGroupEntry::BufferEntry(ctx.m_shBuffer, 0, ShBytes()),
            };
            rhi::BindGroupDesc shBgd{};
            shBgd.layout = m_shLayout;
            shBgd.entries = Span<const rhi::BindGroupEntry>{she, 3};
            if (!m_device->CreateBindGroup(shBgd, ctx.m_shBindGroup).IsOk())
            {
                return false;
            }

            ctx.m_dirty = true; // build the products on the first Prepare
            return true;
        }

        void DestroyContext(Context& ctx)
        {
            DestroyExternalBindGroups(ctx);
            if (ctx.m_shBindGroup)
            {
                m_device->DestroyBindGroup(ctx.m_shBindGroup);
                ctx.m_shBindGroup = nullptr;
            }
            if (ctx.m_envBindGroup)
            {
                m_device->DestroyBindGroup(ctx.m_envBindGroup);
                ctx.m_envBindGroup = nullptr;
            }
            for (u32 m = 0; m < kEnvMips; ++m)
            {
                if (ctx.m_envMipBG[m])
                {
                    m_device->DestroyBindGroup(ctx.m_envMipBG[m]);
                    ctx.m_envMipBG[m] = nullptr;
                }
            }
            if (ctx.m_shBuffer)
            {
                m_device->DestroyBuffer(ctx.m_shBuffer);
                ctx.m_shBuffer = nullptr;
            }
            if (ctx.m_prefilterView)
            {
                m_device->DestroyTextureView(ctx.m_prefilterView);
                ctx.m_prefilterView = nullptr;
            }
            if (ctx.m_prefilterCube)
            {
                m_device->DestroyTexture(ctx.m_prefilterCube);
                ctx.m_prefilterCube = nullptr;
            }
            for (u32 m = 0; m < kEnvMips; ++m)
            {
                if (ctx.m_envMipView[m])
                {
                    m_device->DestroyTextureView(ctx.m_envMipView[m]);
                    ctx.m_envMipView[m] = nullptr;
                }
            }
            if (ctx.m_envSampleView)
            {
                m_device->DestroyTextureView(ctx.m_envSampleView);
                ctx.m_envSampleView = nullptr;
            }
            if (ctx.m_envCube)
            {
                m_device->DestroyTexture(ctx.m_envCube);
                ctx.m_envCube = nullptr;
            }
        }

        bool CreatePipelines()
        {
            rhi::ShaderModule* vs = m_shaders->GetVariant(u8"ibl_fs", shaders::ShaderStage::Vertex,
                                                          shaders::ShaderFlags::None);
            if (vs == nullptr)
            {
                return false;
            }

            // --- env sample bind group layout (t0 cube + s0 sampler), shared by prefilter ---
            rhi::BindGroupLayoutEntry envTex = rhi::BindGroupLayoutEntry::SampledTexture(
                0, rhi::ShaderStage::Fragment, rhi::TextureViewDimension::TextureCube);
            rhi::BindGroupLayoutEntry envSamp =
                rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment);
            rhi::BindGroupLayoutEntry envEntries[] = {envTex, envSamp};
            rhi::BindGroupLayoutDesc envLd{};
            envLd.entries = Span<const rhi::BindGroupLayoutEntry>{envEntries, 2};
            if (!m_device->CreateBindGroupLayout(envLd, m_envLayout).IsOk())
            {
                return false;
            }

            // --- pipeline layouts ---
            rhi::PushConstantRange pcRange{};
            pcRange.stages = rhi::ShaderStage::Fragment;
            pcRange.offset = 0;
            pcRange.size = sizeof(IblPush);
            // procedural env: push constants only.
            rhi::PipelineLayoutDesc envPld{};
            envPld.pushConstantRanges = Span<const rhi::PushConstantRange>{&pcRange, 1};
            if (!m_device->CreatePipelineLayout(envPld, m_envOnlyLayout).IsOk())
            {
                return false;
            }
            // prefilter: env bind group + push constants.
            rhi::BindGroupLayout* preLayouts[] = {m_envLayout};
            rhi::PipelineLayoutDesc prePld{};
            prePld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{preLayouts, 1};
            prePld.pushConstantRanges = Span<const rhi::PushConstantRange>{&pcRange, 1};
            if (!m_device->CreatePipelineLayout(prePld, m_prefilterLayout).IsOk())
            {
                return false;
            }
            // brdf: no inputs.
            rhi::PipelineLayoutDesc brdfPld{};
            if (!m_device->CreatePipelineLayout(brdfPld, m_brdfPipelineLayout).IsOk())
            {
                return false;
            }

            m_envPipeline =
                MakeFullscreenPipeline(vs, u8"ibl_procenv", m_envOnlyLayout, kCubeFormat);
            m_analyticPipeline =
                MakeFullscreenPipeline(vs, u8"ibl_analytic", m_envOnlyLayout, kCubeFormat);
            m_downsamplePipeline =
                MakeFullscreenPipeline(vs, u8"ibl_downsample", m_prefilterLayout, kCubeFormat);
            m_prefilterPipeline =
                MakeFullscreenPipeline(vs, u8"ibl_prefilter", m_prefilterLayout, kCubeFormat);
            m_brdfPipeline =
                MakeFullscreenPipeline(vs, u8"ibl_brdf", m_brdfPipelineLayout, kBrdfFormat);
            if (m_envPipeline == nullptr || m_analyticPipeline == nullptr ||
                m_downsamplePipeline == nullptr || m_prefilterPipeline == nullptr ||
                m_brdfPipeline == nullptr)
            {
                return false;
            }

            // --- SH compute pipeline (t0 cube + s0 sampler + u0 SH buffer; bind groups are per context) ---
            rhi::ShaderModule* cs = m_shaders->GetVariant(u8"ibl_sh", shaders::ShaderStage::Compute,
                                                          shaders::ShaderFlags::None);
            if (cs == nullptr)
            {
                return false;
            }
            rhi::BindGroupLayoutEntry shTex = rhi::BindGroupLayoutEntry::SampledTexture(
                0, rhi::ShaderStage::Compute, rhi::TextureViewDimension::TextureCube);
            rhi::BindGroupLayoutEntry shSamp =
                rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Compute);
            rhi::BindGroupLayoutEntry shOut = rhi::BindGroupLayoutEntry::StorageBuffer(
                0, rhi::ShaderStage::Compute, /*readOnly*/ false);
            rhi::BindGroupLayoutEntry shEntries[] = {shTex, shSamp, shOut};
            rhi::BindGroupLayoutDesc shLd{};
            shLd.entries = Span<const rhi::BindGroupLayoutEntry>{shEntries, 3};
            if (!m_device->CreateBindGroupLayout(shLd, m_shLayout).IsOk())
            {
                return false;
            }
            rhi::BindGroupLayout* shLayouts[] = {m_shLayout};
            rhi::PipelineLayoutDesc shPld{};
            shPld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{shLayouts, 1};
            if (!m_device->CreatePipelineLayout(shPld, m_shPipelineLayout).IsOk())
            {
                return false;
            }
            rhi::ComputePipelineDesc cpd{};
            cpd.layout = m_shPipelineLayout;
            cpd.compute = rhi::ProgrammableStage{cs, u8"main", rhi::ShaderStage::Compute};
            cpd.label = u8"ibl.sh";
            if (!m_device->CreateComputePipeline(cpd, m_shPipeline).IsOk())
            {
                return false;
            }

            m_ready = true;
            return true;
        }

        rhi::RenderPipeline* MakeFullscreenPipeline(rhi::ShaderModule* vs, StringView psName,
                                                    rhi::PipelineLayout* layout,
                                                    rhi::TextureFormat fmt)
        {
            rhi::ShaderModule* ps = m_shaders->GetVariant(psName, shaders::ShaderStage::Fragment,
                                                          shaders::ShaderFlags::None);
            if (ps == nullptr)
            {
                return nullptr;
            }
            rhi::ColorTargetState color{};
            color.format = fmt;
            rhi::FragmentState frag{};
            frag.shader = rhi::ProgrammableStage{ps, u8"main", rhi::ShaderStage::Fragment};
            frag.targets = Span<const rhi::ColorTargetState>{&color, 1};
            rhi::RenderPipelineDesc pd{};
            pd.layout = layout;
            pd.vertex.shader = rhi::ProgrammableStage{vs, u8"main", rhi::ShaderStage::Vertex};
            pd.fragment = frag;
            pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
            pd.primitive.cullMode = rhi::CullMode::None;
            pd.label = psName;
            rhi::RenderPipeline* p = nullptr;
            if (!m_device->CreateRenderPipeline(pd, p).IsOk())
            {
                return nullptr;
            }
            return p;
        }

        // Concatenate two shader source literals into an owned String (IblCommon() + a PS body).
        static String Concat(StringView a, StringView b)
        {
            String s(a);
            s.Append(b);
            return s;
        }

        // Lazily create the equirect->cube pipeline (2D source tex + sampler + push) - only when an HDR
        // equirect is first set, since most scenes are procedural.
        bool EnsureEquirectPipeline()
        {
            if (m_equirectPipeline != nullptr)
            {
                return true;
            }
            rhi::ShaderModule* vs = m_shaders->GetVariant(u8"ibl_fs", shaders::ShaderStage::Vertex,
                                                          shaders::ShaderFlags::None);
            if (vs == nullptr)
            {
                return false;
            }
            rhi::BindGroupLayoutEntry tex = rhi::BindGroupLayoutEntry::SampledTexture(
                0, rhi::ShaderStage::Fragment, rhi::TextureViewDimension::Texture2D);
            rhi::BindGroupLayoutEntry samp =
                rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment);
            rhi::BindGroupLayoutEntry e[] = {tex, samp};
            rhi::BindGroupLayoutDesc ld{};
            ld.entries = Span<const rhi::BindGroupLayoutEntry>{e, 2};
            if (!m_device->CreateBindGroupLayout(ld, m_equirectLayout).IsOk())
            {
                return false;
            }
            rhi::PushConstantRange pc{};
            pc.stages = rhi::ShaderStage::Fragment;
            pc.offset = 0;
            pc.size = sizeof(IblPush);
            rhi::BindGroupLayout* layouts[] = {m_equirectLayout};
            rhi::PipelineLayoutDesc pld{};
            pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{layouts, 1};
            pld.pushConstantRanges = Span<const rhi::PushConstantRange>{&pc, 1};
            if (!m_device->CreatePipelineLayout(pld, m_equirectPipelineLayout).IsOk())
            {
                return false;
            }
            m_equirectPipeline =
                MakeFullscreenPipeline(vs, u8"ibl_equirect", m_equirectPipelineLayout, kCubeFormat);
            if (m_equirectPipeline == nullptr)
            {
                return false;
            }
            rhi::SamplerDesc ss{};
            ss.minFilter = rhi::FilterMode::Linear;
            ss.magFilter = rhi::FilterMode::Linear;
            ss.mipmapFilter = rhi::MipmapFilterMode::Linear;
            ss.addressU = rhi::AddressMode::Repeat;
            ss.addressV = rhi::AddressMode::ClampToEdge;
            ss.addressW = rhi::AddressMode::ClampToEdge;
            ss.label = u8"ibl.equirectSampler";
            if (!m_device->CreateSampler(ss, m_equirectSampler).IsOk())
            {
                return false;
            }
            return true;
        }

        // Lazily create the cubemap->cube pipeline (samples the source cube; reuses the prefilter's cube
        // bind-group + pipeline layout - cube tex + sampler + push).
        bool EnsureCubemapPipeline()
        {
            if (m_cubemapPipeline != nullptr)
            {
                return true;
            }
            if (m_prefilterLayout == nullptr || m_envLayout == nullptr)
            {
                return false;
            }
            rhi::ShaderModule* vs = m_shaders->GetVariant(u8"ibl_fs", shaders::ShaderStage::Vertex,
                                                          shaders::ShaderFlags::None);
            if (vs == nullptr)
            {
                return false;
            }
            m_cubemapPipeline =
                MakeFullscreenPipeline(vs, u8"ibl_cubemap", m_prefilterLayout, kCubeFormat);
            return m_cubemapPipeline != nullptr;
        }

        void DestroyCubemap()
        {
            if (m_cubemapBindGroup)
            {
                m_device->DestroyBindGroup(m_cubemapBindGroup);
                m_cubemapBindGroup = nullptr;
            }
            if (m_cubemapStaging)
            {
                m_device->DestroyBuffer(m_cubemapStaging);
                m_cubemapStaging = nullptr;
            }
            if (m_srcCubeView)
            {
                m_device->DestroyTextureView(m_srcCubeView);
                m_srcCubeView = nullptr;
            }
            if (m_srcCube)
            {
                m_device->DestroyTexture(m_srcCube);
                m_srcCube = nullptr;
            }
            m_cubemapPending = false;
        }

        // Free the per-source equirect texture/staging/view/bind-group (the pipeline + layout + sampler
        // persist, recreated lazily once).
        void DestroyEquirect()
        {
            if (m_equirectBindGroup)
            {
                m_device->DestroyBindGroup(m_equirectBindGroup);
                m_equirectBindGroup = nullptr;
            }
            if (m_equirectStaging)
            {
                m_device->DestroyBuffer(m_equirectStaging);
                m_equirectStaging = nullptr;
            }
            if (m_equirectView)
            {
                m_device->DestroyTextureView(m_equirectView);
                m_equirectView = nullptr;
            }
            if (m_equirectTex)
            {
                m_device->DestroyTexture(m_equirectTex);
                m_equirectTex = nullptr;
            }
            m_equirectPending = false;
        }

        void Shutdown()
        {
            for (const UniquePtr<Context>& ctx : m_contexts)
            {
                DestroyContext(*ctx);
            }
            m_contexts.Clear();
            DestroyCubemap();
            if (m_cubemapPipeline)
            {
                m_device->DestroyRenderPipeline(m_cubemapPipeline);
                m_cubemapPipeline = nullptr;
            }
            DestroyEquirect();
            if (m_equirectPipeline)
            {
                m_device->DestroyRenderPipeline(m_equirectPipeline);
                m_equirectPipeline = nullptr;
            }
            if (m_equirectPipelineLayout)
            {
                m_device->DestroyPipelineLayout(m_equirectPipelineLayout);
                m_equirectPipelineLayout = nullptr;
            }
            if (m_equirectLayout)
            {
                m_device->DestroyBindGroupLayout(m_equirectLayout);
                m_equirectLayout = nullptr;
            }
            if (m_equirectSampler)
            {
                m_device->DestroySampler(m_equirectSampler);
                m_equirectSampler = nullptr;
            }
            if (m_shPipeline)
            {
                m_device->DestroyComputePipeline(m_shPipeline);
                m_shPipeline = nullptr;
            }
            if (m_envPipeline)
            {
                m_device->DestroyRenderPipeline(m_envPipeline);
                m_envPipeline = nullptr;
            }
            if (m_analyticPipeline)
            {
                m_device->DestroyRenderPipeline(m_analyticPipeline);
                m_analyticPipeline = nullptr;
            }
            if (m_downsamplePipeline)
            {
                m_device->DestroyRenderPipeline(m_downsamplePipeline);
                m_downsamplePipeline = nullptr;
            }
            if (m_prefilterPipeline)
            {
                m_device->DestroyRenderPipeline(m_prefilterPipeline);
                m_prefilterPipeline = nullptr;
            }
            if (m_brdfPipeline)
            {
                m_device->DestroyRenderPipeline(m_brdfPipeline);
                m_brdfPipeline = nullptr;
            }
            if (m_shPipelineLayout)
            {
                m_device->DestroyPipelineLayout(m_shPipelineLayout);
                m_shPipelineLayout = nullptr;
            }
            if (m_envOnlyLayout)
            {
                m_device->DestroyPipelineLayout(m_envOnlyLayout);
                m_envOnlyLayout = nullptr;
            }
            if (m_prefilterLayout)
            {
                m_device->DestroyPipelineLayout(m_prefilterLayout);
                m_prefilterLayout = nullptr;
            }
            if (m_brdfPipelineLayout)
            {
                m_device->DestroyPipelineLayout(m_brdfPipelineLayout);
                m_brdfPipelineLayout = nullptr;
            }
            if (m_shLayout)
            {
                m_device->DestroyBindGroupLayout(m_shLayout);
                m_shLayout = nullptr;
            }
            if (m_envLayout)
            {
                m_device->DestroyBindGroupLayout(m_envLayout);
                m_envLayout = nullptr;
            }
            if (m_sampler)
            {
                m_device->DestroySampler(m_sampler);
                m_sampler = nullptr;
            }
            if (m_brdfView)
            {
                m_device->DestroyTextureView(m_brdfView);
                m_brdfView = nullptr;
            }
            if (m_brdfLut)
            {
                m_device->DestroyTexture(m_brdfLut);
                m_brdfLut = nullptr;
            }
        }

        rhi::Device* m_device;
        shaders::ShaderSystem* m_shaders;

        // Per-scene contexts (UniquePtr = stable addresses; frame lambdas capture their bind groups).
        Array<UniquePtr<Context>> m_contexts;
        u64 m_frame = 0;
        u64 m_nextGeneration = 0; // system-wide: context generations never collide
        u64 m_nextContextUid = 0; // stable context identities (never reused)
        u64 m_sourceStamp =
            1; // programmatic SetEquirect/SetCubemap change tick (contexts start at 0)

        // Shared PROGRAMMATIC HDR equirect source: uploaded 2D texture sampled by the equirect->cube pass.
        rhi::Texture* m_equirectTex = nullptr;
        rhi::TextureView* m_equirectView = nullptr;
        rhi::Buffer* m_equirectStaging = nullptr;
        rhi::Sampler* m_equirectSampler = nullptr;
        rhi::BindGroupLayout* m_equirectLayout = nullptr;
        rhi::PipelineLayout* m_equirectPipelineLayout = nullptr;
        rhi::RenderPipeline* m_equirectPipeline = nullptr;
        rhi::BindGroup* m_equirectBindGroup = nullptr;
        u32 m_equirectW = 0, m_equirectH = 0;
        bool m_equirectPending = false;

        // Shared PROGRAMMATIC cubemap source.
        rhi::Texture* m_srcCube = nullptr;
        rhi::TextureView* m_srcCubeView = nullptr;
        rhi::Buffer* m_cubemapStaging = nullptr;
        rhi::RenderPipeline* m_cubemapPipeline = nullptr; // reuses m_prefilterLayout + m_envLayout
        rhi::BindGroup* m_cubemapBindGroup = nullptr;
        u32 m_cubemapFaceSize = 0;
        bool m_cubemapPending = false;

        // Shared products + machinery.
        rhi::Texture* m_brdfLut = nullptr;
        rhi::TextureView* m_brdfView = nullptr;
        rhi::Sampler* m_sampler = nullptr;

        rhi::BindGroupLayout* m_envLayout = nullptr;
        rhi::BindGroupLayout* m_shLayout = nullptr;
        rhi::PipelineLayout* m_envOnlyLayout = nullptr;
        rhi::PipelineLayout* m_prefilterLayout = nullptr;
        rhi::PipelineLayout* m_brdfPipelineLayout = nullptr;
        rhi::PipelineLayout* m_shPipelineLayout = nullptr;
        rhi::RenderPipeline* m_envPipeline = nullptr;
        rhi::RenderPipeline* m_analyticPipeline =
            nullptr; // Preetham; reuses m_envOnlyLayout (push only)
        rhi::RenderPipeline* m_downsamplePipeline =
            nullptr; // env mip pyramid; reuses m_prefilterLayout
        rhi::RenderPipeline* m_prefilterPipeline = nullptr;
        rhi::RenderPipeline* m_brdfPipeline = nullptr;
        rhi::ComputePipeline* m_shPipeline = nullptr;

        rhi::ResourceState m_brdfState = rhi::ResourceState::Undefined;
        rendergraph::RGHandle m_brdfH = {};

        bool m_ready = false;
        bool m_brdfDone = false; // the BRDF LUT is constant - generated once, not per sky change
    };

} // namespace draconic::render
