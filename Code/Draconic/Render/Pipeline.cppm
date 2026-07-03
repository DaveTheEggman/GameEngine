/// Draconic::Render — the `:pipeline` partition.
///
/// The frame driver + extension seam. A `Renderer` is a per-category drawer; subsystems
/// (meshes here, particles/sprites/world-UI later) register one with the `RendererRegistry`
/// and contribute draws with ZERO core changes — the keeper architecture validated by
/// Sedulous's particles being a separate library. The core sorts a view's `DrawItem`s by
/// category and dispatches each run to its registered `Renderer`. (§6.)
///
/// `RenderFrame` is the SINGLE per-frame driver (not a per-view god object — the explicit
/// replacement for Sedulous's Pipeline/ShadowPipeline/ProbePipeline). Its lifecycle mirrors
/// Sedulous's useful `ISceneRenderer` shape — Begin / AddView×N / End — so multiple scenes
/// and views share per-frame state (the view pool, the renderers' transient buffers) without
/// clobbering each other. Views are collected, then composed together at End. (§9.) Phase 1
/// records directly into the command encoder via `ForwardPass`; phase 3 routes the same
/// pass-group through draconic.rendergraph (MRT + automatic barriers + transient aliasing).

module;
#include "Core/Prelude.h"
#include "Profiler/Profiler.h"

export module draconic.render:pipeline;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.profiler;
import :data;
import :views;
import :cluster_system;
import :tonemap;
import :shadows;
import :ibl;
import :probes;
import :bloom;
import :taa;
import :ao;
import :fxaa;
import :debug_draw;
import :debug_pass;
import :decal_pass;
import :sky;

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// Halton(base) low-discrepancy sequence term (1-based index).
[[nodiscard]] inline f32 HaltonSeq(u32 i, u32 base) {
    f32 f = 1.0f, r = 0.0f;
    u32 idx = i + 1u;
    while (idx > 0u) { f /= static_cast<f32>(base); r += f * static_cast<f32>(idx % base); idx /= base; }
    return r;
}
// TAA sub-pixel jitter for frame `index` (mod the sequence length), in clip space (matches Sedulous):
// Halton(2,3) centered to [-0.5,0.5], scaled to a 1-texel clip offset. Add to projection (2,0)/(2,1).
[[nodiscard]] inline Vec2 HaltonJitter(u32 index, u32 width, u32 height) {
    const f32 x = HaltonSeq(index, 2u) - 0.5f;
    const f32 y = HaltonSeq(index, 3u) - 0.5f;
    return Vec2{ x * 2.0f / static_cast<f32>(width  > 0 ? width  : 1u),
                 y * 2.0f / static_cast<f32>(height > 0 ? height : 1u) };
}

// What a Renderer needs to record draws for one view. `pass` is a RenderCommandEncoder — the
// shared draw-recording surface — so a renderer records identically whether it targets a live
// render pass or an off-thread render bundle (the basis for parallel command recording).
struct RenderRecordContext {
    const RenderView*          view        = nullptr;
    rhi::RenderCommandEncoder* pass        = nullptr;
    Mat4                       viewProj    = Mat4::Identity();
    Mat4                       prevViewProj = Mat4::Identity();  // last frame's view-proj (camera motion vectors)
    Vec2                       jitter      = Vec2{ 0, 0 };       // this frame's NDC sub-pixel TAA jitter
    Vec2                       prevJitter  = Vec2{ 0, 0 };       // last frame's jitter (unjitter the reprojection)
    Mat4                       viewMatrix  = Mat4::Identity();   // for view-space depth (clustered shading)
    Vec3                       cameraPos   = Vec3{ 0, 0, 0 };
    Vec3                       ambient     = Vec3{ 0.03f, 0.03f, 0.03f };   // scene environment ambient
    ShadowCascades             cascades    = {};                 // this view's CSM cascades (phase 5.2)
    u32                        cascadeLayerBase = 0;             // this view's first shadow-array layer
    Span<const GpuLight>       lights      = {};
    ClusterBinding             cluster     = {};                 // per-cluster light lists (empty = clustering off)
    u32                        frameIndex  = 0;
    u32                        viewIndex   = 0;                  // this view's index in the frame (per-view buffer slots)
    rhi::TextureFormat         colorFormat = rhi::TextureFormat::BGRA8Unorm;
    rhi::TextureFormat         depthFormat = rhi::TextureFormat::Depth32Float;
    bool                       depthPrepass = false;   // camera depth-only prepass: no depth bias (match forward exactly)
    bool                       probesEnabled = true;    // false during probe capture: reflect the sky IBL, not the probe (no feedback/self-black)
};

// A fully-resolved draw: all GPU state resolved (PSO built, bind groups + ring slots allocated,
// buffers bound), ready to EMIT as pure commands with NO shared mutation — so emission can run
// in parallel across threads/bundles. Produced by Renderer::Resolve (single-threaded, where the
// allocation/upload/caching happens); replayed by EmitDraw. Backend-agnostic (all RHI handles),
// so the emit phase is renderer-agnostic.
// Bind groups are named by the engine's set-frequency convention (set 0 = view/per-frame,
// set 1 = per-draw object/instance, set 2 = material), which every renderer follows.
struct ResolvedDraw {
    rhi::RenderPipeline* pso          = nullptr;
    rhi::BindGroup*      viewSet      = nullptr;   // set 0 (view)
    u32                  viewOffset   = 0;
    bool                 viewDynamic  = false;
    rhi::BindGroup*      drawSet      = nullptr;   // set 1 (object UBO / instance storage)
    u32                  drawOffset   = 0;
    bool                 drawDynamic  = false;
    rhi::BindGroup*      materialSet  = nullptr;   // set 2 (material — inferred from properties)
    rhi::BindGroup*      clusterSet   = nullptr;   // set 3 (clustered light lists; dummy when off)
    rhi::Buffer*         vertexBuffer0 = nullptr;  u64 vertexOffset0 = 0;
    rhi::Buffer*         vertexBuffer1 = nullptr;  u64 vertexOffset1 = 0;   // optional: skin stream (skinned) or instance offsets
    rhi::Buffer*         vertexBuffer2 = nullptr;  u64 vertexOffset2 = 0;   // optional: instance offsets (skinned+instanced)
    rhi::Buffer*         indexBuffer  = nullptr;   u64 indexOffset = 0;
    rhi::IndexFormat     indexFormat  = rhi::IndexFormat::UInt32;
    u32                  indexCount   = 0;
    u32                  instanceCount = 1;
};

// What the directional shadow pass exposes to the forward pass: the depth map's sample view (bound
// in set 0 by the mesh renderer), the graph handle (ReadTexture'd so the forward is ordered after
// the depth write + the map barriers to a shader-readable state), and the light-space matrix.
// Defined here (not in :shadows) because the forward pass consumes it and :shadows imports :pipeline.
struct ShadowBinding {
    rhi::TextureView*     sampleView = nullptr;   // the cascade depth ARRAY (all views' layers)
    rendergraph::RGHandle handle     = {};        // ReadTexture'd to order the cascade writes -> forward
    ShadowCascades        cascades;               // THIS view's cascade matrices/splits
    u32                   layerBase  = 0;         // this view's first array layer (viewIndex * cascades)
    bool                  valid      = false;
    // Local-light (spot/point) shadow atlas (5.3) — scene-global, one atlas shared by all views.
    // ReadTexture'd by every forward pass so the atlas depth pass is ordered + barriered ahead of it.
    rendergraph::RGHandle atlasHandle = {};
    bool                  atlasValid  = false;
    [[nodiscard]] bool Valid() const noexcept { return valid && sampleView != nullptr; }
};

// What the IBL precompute exposes to the forward pass: this frame's graph handles for the products the
// forward samples in set 0 (prefiltered specular cube, BRDF LUT, SH9 diffuse buffer). ReadTexture'd /
// ReadBuffer'd so the graph orders any precompute writes -> forward and barriers them shader-readable.
struct IblBinding {
    rendergraph::RGHandle prefilterHandle = {};
    rendergraph::RGHandle brdfHandle = {};
    rendergraph::RGHandle shHandle = {};
    bool                  valid = false;
    [[nodiscard]] bool Valid() const noexcept { return valid; }
};

// Replay one resolved draw into any command sink (a live pass or an off-thread bundle). Pure
// command emission — touches no shared state, so it is safe to run concurrently.
inline void EmitDraw(rhi::RenderCommandEncoder& enc, const ResolvedDraw& d) {
    if (d.pso == nullptr || d.indexBuffer == nullptr) { return; }
    enc.SetPipeline(d.pso);
    if (d.viewSet != nullptr) {
        if (d.viewDynamic) { enc.SetBindGroup(0, d.viewSet, Span<const u32>{ &d.viewOffset, 1 }); }
        else               { enc.SetBindGroup(0, d.viewSet, Span<const u32>{}); }
    }
    if (d.drawSet != nullptr) {
        if (d.drawDynamic) { enc.SetBindGroup(1, d.drawSet, Span<const u32>{ &d.drawOffset, 1 }); }
        else               { enc.SetBindGroup(1, d.drawSet, Span<const u32>{}); }
    }
    if (d.materialSet != nullptr) { enc.SetBindGroup(2, d.materialSet, Span<const u32>{}); }   // material
    if (d.clusterSet != nullptr) { enc.SetBindGroup(3, d.clusterSet, Span<const u32>{}); }     // cluster lists
    if (d.vertexBuffer0 != nullptr) { enc.SetVertexBuffer(0, d.vertexBuffer0, d.vertexOffset0); }
    if (d.vertexBuffer1 != nullptr) { enc.SetVertexBuffer(1, d.vertexBuffer1, d.vertexOffset1); }
    if (d.vertexBuffer2 != nullptr) { enc.SetVertexBuffer(2, d.vertexBuffer2, d.vertexOffset2); }
    enc.SetIndexBuffer(d.indexBuffer, d.indexFormat, d.indexOffset);
    enc.DrawIndexed(d.indexCount, d.instanceCount);
}

// A per-category drawer. Implemented by mesh/sprite/particle/etc. subsystems and registered with
// the RendererRegistry. Two phases: RESOLVE turns a sorted run of DrawItems into ResolvedDraws
// (single-threaded — this is where mesh upload, PSO build, and ring allocation happen); the
// ForwardPass then EMITs the resolved draws (serially or in parallel) with no shared mutation.
// PrepareFrame/FinishFrame bracket the whole frame so a renderer sizes its transient buffers once.
class Renderer {
public:
    virtual ~Renderer() = default;

    // The categories this renderer draws (its registration keys).
    [[nodiscard]] virtual Span<const RenderCategory> SupportedCategories() const = 0;

    // Bracket the frame: `maxDraws` is an upper bound on DrawItems this renderer may receive
    // across all views, so per-object transient (e.g. the object-UBO ring) is sized once and
    // never reallocated mid-frame (which would invalidate already-resolved draws). `frameIndex`
    // is the device ring slot, selecting this frame's region of any frames-in-flight ring.
    virtual void PrepareFrame(u32 maxDraws, u32 frameIndex) { (void)maxDraws; (void)frameIndex; }

    // Resolve a sorted run of this renderer's DrawItems into `out` (append). Single-threaded:
    // all GPU-state mutation (mesh upload, PSO build, ring allocation + writes) happens here.
    virtual void Resolve(const RenderRecordContext& ctx, Span<const DrawItem> items, Array<ResolvedDraw>& out) = 0;

    // Resolve the same draws as DEPTH-ONLY casters for a shadow pass: `ctx.viewProj` is the light's
    // world->clip matrix and `ctx.depthFormat` the shadow map's format. Produces depth-only
    // ResolvedDraws (set 0 = light view, set 1 = object/instance; no material/cluster). Default
    // no-op so a renderer opts in to casting shadows (the mesh renderer does; sprites need not).
    virtual void ResolveDepthOnly(const RenderRecordContext& ctx, Span<const DrawItem> items, Array<ResolvedDraw>& out) {
        (void)ctx; (void)items; (void)out;
    }

    // Hand this frame's directional shadow map (null = none) to a renderer that samples it in set 0,
    // with the ShadowSystem's generation (bumped on texture recreation) so the renderer's bind-group
    // cache invalidates on a reused-address view. Called once per frame before PrepareFrame. Default
    // no-op (a renderer that doesn't shade ignores it).
    virtual void SetShadowMap(rhi::TextureView* shadowMap, u64 generation) { (void)shadowMap; (void)generation; }

    // Hand this frame's local-light (spot/point) shadow atlas (null = none) + its generation, same
    // contract/timing as SetShadowMap. `passCount` is how many atlas depth passes (one per caster
    // tile) will re-emit this renderer's casters, so it can size its per-object rings. Default no-op.
    virtual void SetShadowAtlas(rhi::TextureView* atlas, u64 generation, u32 passCount) { (void)atlas; (void)generation; (void)passCount; }

    // How many reflection-probe capture faces will re-emit this renderer's draws this frame (one forward
    // pass per face). Lets it size its per-object rings for the extra draw-resolving passes. Called once
    // per frame before PrepareFrame. Default no-op.
    virtual void SetCaptureFacePasses(u32 passes) { (void)passes; }

    // This frame's reflection probes: the prefiltered cube-ARRAY (set-0 t8) + the probe-metadata SRV (t9)
    // + the active probe count (the forward loops Probes[0..count]). null => no probes. Called once per
    // frame before PrepareFrame. Default no-op.
    virtual void SetProbes(rhi::TextureView* cubeArray, rhi::Buffer* probeBuffer, u32 count) {
        (void)cubeArray; (void)probeBuffer; (void)count;
    }

    // Upload this frame's local-shadow entries (the atlas's per-light matrices/rects) for a renderer
    // that binds them in set 0. Called once per frame after PrepareFrame. Default no-op.
    virtual void UploadLocalShadows(Span<const GpuLocalShadow> shadows, u32 frameIndex) { (void)shadows; (void)frameIndex; }

    // Hand this frame's IBL products to a renderer that samples them in set 0 (SH9 diffuse buffer +
    // prefiltered specular cube + BRDF LUT), with the IBLSystem's generation for bind-group cache
    // invalidation and the prefilter's max LOD (roughness -> mip). null views => the renderer uses its
    // neutral fallbacks (flat ambient). Called once per frame before PrepareFrame. Default no-op.
    virtual void SetIBL(rhi::Buffer* sh, rhi::TextureView* prefilter, rhi::TextureView* brdf,
                        f32 maxLod, u64 generation) {
        (void)sh; (void)prefilter; (void)brdf; (void)maxLod; (void)generation;
    }

    // Pre-pass: write this frame's skinning matrices into the renderer's persistent bone pool ONCE
    // (current + previous slab per distinct skeleton instance) and copy staging->device on `encoder`.
    // Called once per frame after PrepareFrame and BEFORE the render graph executes, so the forward
    // and shadow passes share one device-local bone buffer (no per-pass re-upload). Default no-op.
    virtual void UploadSkinning(const ExtractedScene& scene, rhi::CommandEncoder& encoder) { (void)scene; (void)encoder; }

    virtual void FinishFrame() {}

    // This renderer's dispatch id — its index in the RendererRegistry, assigned at Register. Producers
    // stamp it onto their RenderData::rendererId so emission routes each draw back to its owner (so
    // several renderers can share a category and still be dispatched correctly). Set by the registry.
    [[nodiscard]] u16  RendererId() const noexcept { return m_rendererId; }
    void SetRendererId(u16 id) noexcept { m_rendererId = id; }

private:
    u16 m_rendererId = 0;
};

// Holds the registered renderers and dispatches a draw to its owner by RenderData::rendererId.
// Dispatch is per-item (not per-category) so several renderers can share a category and still be
// routed correctly (ezEngine-style). The id is the renderer's registration index; the FIRST renderer
// registered gets id 0, which is the RenderData::rendererId default (so plain mesh data needs no tag).
class RendererRegistry {
public:
    // Register `renderer` (borrowed; the caller owns its lifetime); assigns its dispatch id.
    void Register(Renderer* renderer) {
        if (renderer == nullptr) { return; }
        renderer->SetRendererId(static_cast<u16>(m_unique.Size()));
        m_unique.PushBack(renderer);
    }

    [[nodiscard]] Renderer* ById(u16 id) const noexcept {
        return (id < m_unique.Size()) ? m_unique[id] : nullptr;
    }

    [[nodiscard]] Span<Renderer* const> Unique() const noexcept {
        return Span<Renderer* const>{ m_unique.Data(), m_unique.Size() };
    }

private:
    Array<Renderer*> m_unique;
};

// The phase-1 forward pass: opens one render pass against a view's color target + an owned
// depth buffer, then dispatches the view's sorted draw list to the registered Renderers,
// one contiguous category-run at a time. (Phase 3 replaces this with graph-scheduled passes:
// depth prepass + MRT forward + post, with automatic barriers and transient aliasing.)
class ForwardPass {
public:
    ForwardPass(rhi::Device& device, u32 framesInFlight) noexcept
        : m_device(&device), m_framesInFlight(framesInFlight < 1 ? 1 : framesInFlight) {}
    ~ForwardPass() { ReleaseWorkerPools(); }

    ForwardPass(const ForwardPass&) = delete;
    ForwardPass& operator=(const ForwardPass&) = delete;

    // Once per frame, before composing views: provision + reset this frame's per-worker command
    // pools (used for parallel emit). Reset happens ONCE per frame — a worker bundle's secondary
    // command buffer must outlive the main submission that executes it, so it can't be freed
    // between views. (No-op when the job system is absent — emit then runs serially.)
    void BeginFrame(u32 frameIndex) {
        if (!HasGlobalJobSystem()) { return; }
        if (!EnsureWorkerPools(GlobalJobs().SlotCount())) { return; }
        const u32 base = frameIndex * m_workerSlots;
        for (u32 s = 0; s < m_workerSlots; ++s) {
            if (m_workerPools[base + s] != nullptr) { m_workerPools[base + s]->Reset(); }
        }
    }

    // Declare this view's forward pass into the frame graph: a TRANSIENT depth target (the graph
    // allocates it + inserts the depth barrier automatically — retiring the hand-rolled depth
    // transition) + the IMPORTED color target (left in RenderTarget for the host to present). The
    // pass body is a render bundle the graph executes (secondary contents). Resolve + emit run in
    // the bundle callback at graph Execute time.
    // `colorH` is the (shared) imported target handle; `clearColor` is true for the first view that
    // writes a given target (it clears the whole target), false for later views into the same target
    // (they Load so they don't wipe earlier views' regions). Depth is a per-view transient (each clears).
    // `colorH` is the target the forward writes (an HDR transient when tonemapping, else the imported
    // LDR target); `colorFormat` is its format (so the PSO matches). `clearColor` clears vs loads.
    [[nodiscard]] rhi::TextureFormat DepthFormat() const noexcept { return m_depthFormat; }

    void DeclarePass(const RenderView& view, const RendererRegistry& registry,
                     rendergraph::RenderGraph& graph, u32 frameIndex, u32 viewIndex,
                     rendergraph::RGHandle colorH, rendergraph::RGHandle depth, bool clearColor, rhi::TextureFormat colorFormat,
                     rendergraph::RGHandle normalH, rendergraph::RGHandle velocityH,
                     const Mat4& prevViewProj, Vec2 jitter, Vec2 prevJitter,
                     const ClusterBinding& cluster = {}, const ShadowBinding& shadow = {},
                     const IblBinding& ibl = {},
                     rhi::LoadOp depthLoad = rhi::LoadOp::Load,
                     rendergraph::RGSubresourceRange colorSub = {},
                     rendergraph::RGHandle probeHandle = {}, bool probeValid = false) {
        if (view.Width() == 0 || view.Height() == 0) { return; }

        const rhi::LoadOp colorLoad = clearColor ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
        graph.AddRenderPass(u8"forward", [this, &view, &registry, depth, colorH, normalH, velocityH, colorLoad, colorFormat, frameIndex, viewIndex, prevViewProj, jitter, prevJitter, cluster, shadow, ibl, depthLoad, colorSub, probeHandle, probeValid](rendergraph::PassBuilder& b) {
            // colorSub targets a single layer when capturing into a cube-array face (default {} = whole target).
            b.SetColorTarget(0, colorH, colorLoad, rhi::StoreOp::Store, view.Settings().clear, colorSub);
            // MRT G-buffer aux (cleared each view): view-space normal + screen-space motion vector.
            b.SetColorTarget(1, normalH, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black());
            b.SetColorTarget(2, velocityH, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black());
            // Depth: loaded after the prepass for early-Z; capture (no prepass) passes Clear.
            b.SetDepthTarget(depth, depthLoad, rhi::StoreOp::Store);
            // Render into this view's viewport sub-rect of the target (split-screen).
            b.SetViewport(view.ViewportX(), view.ViewportY(), view.ViewportWidth(), view.ViewportHeight());
            // Read the cluster lists the build compute pass wrote (orders compute -> this pass).
            if (cluster.Valid()) { b.ReadBuffer(cluster.offsetsHandle); b.ReadBuffer(cluster.indicesHandle); }
            // Read the WHOLE cascade array (orders all cascade passes -> this pass + barriers every
            // layer readable). The forward shader binds the full-array sample view, so the descriptor
            // spans all layers — every one must be in DepthStencilRead when this pass's secondary CB
            // samples it, even layers belonging to other views (VUID-vkCmdExecuteCommands depth-layout).
            // All cascade passes are declared up front, so depending on the whole array is correctly ordered.
            if (shadow.Valid()) { b.SampleDepth(shadow.handle); }
            // Read the whole local-shadow atlas (orders the atlas depth pass -> this pass + barriers
            // it readable). One atlas shared by all views, so the whole texture is the dependency.
            if (shadow.atlasValid) { b.SampleDepth(shadow.atlasHandle); }
            // Read the IBL products (orders any precompute writes -> this pass + barriers them readable).
            if (ibl.Valid()) { b.ReadTexture(ibl.prefilterHandle); b.ReadTexture(ibl.brdfHandle); b.ReadBuffer(ibl.shHandle); }
            // Read the probe captured cube-array (orders probe capture -> this forward + barriers the whole
            // array to ShaderRead, incl. uncaptured slices) — the forward samples it (t8) for local reflections.
            if (probeValid) { b.ReadTexture(probeHandle); }
            b.NeverCull();
            b.SetBundleExecute([this, &view, &registry, colorFormat, frameIndex, viewIndex, prevViewProj, jitter, prevJitter, cluster, shadow, probeValid](rhi::CommandEncoder& enc, Array<rhi::RenderBundle*>& out) {
                ResolveAndEmit(view, registry, enc, frameIndex, viewIndex, colorFormat, view.Camera().ViewProjection(), prevViewProj, jitter, prevJitter, /*transparentPass*/ false, cluster, shadow, out, /*probesEnabled*/ probeValid);
            });
        });
    }

    // The transparent pass: blended geometry into the (already-lit) color target only — no G-buffer, no
    // depth write. Runs after opaque + sky, depth-tested read-only against the opaque depth, back-to-front
    // (the draw list is pre-sorted). Transparent is lit, so it still binds the cluster/shadow/IBL set-0
    // resources. colorH is loaded (preserves opaque + sky); the PSO is the color-only forward permutation.
    void DeclareTransparent(const RenderView& view, const RendererRegistry& registry,
                            rendergraph::RenderGraph& graph, u32 frameIndex, u32 viewIndex,
                            rendergraph::RGHandle colorH, rendergraph::RGHandle depth, rhi::TextureFormat colorFormat,
                            const Mat4& drawViewProj, const Mat4& prevViewProj, Vec2 jitter, Vec2 prevJitter,
                            const ClusterBinding& cluster = {}, const ShadowBinding& shadow = {},
                            const IblBinding& ibl = {}) {
        if (view.Width() == 0 || view.Height() == 0) { return; }
        graph.AddRenderPass(u8"transparent", [this, &view, &registry, depth, colorH, colorFormat, frameIndex, viewIndex, drawViewProj, prevViewProj, jitter, prevJitter, cluster, shadow, ibl](rendergraph::PassBuilder& b) {
            b.SetColorTarget(0, colorH, rhi::LoadOp::Load, rhi::StoreOp::Store, view.Settings().clear);
            b.SetReadOnlyDepthTarget(depth);   // test against opaque depth, no write
            b.SetViewport(view.ViewportX(), view.ViewportY(), view.ViewportWidth(), view.ViewportHeight());
            if (cluster.Valid()) { b.ReadBuffer(cluster.offsetsHandle); b.ReadBuffer(cluster.indicesHandle); }
            if (shadow.Valid()) { b.SampleDepth(shadow.handle); }
            if (shadow.atlasValid) { b.SampleDepth(shadow.atlasHandle); }
            if (ibl.Valid()) { b.ReadTexture(ibl.prefilterHandle); b.ReadTexture(ibl.brdfHandle); b.ReadBuffer(ibl.shHandle); }
            b.NeverCull();
            b.SetBundleExecute([this, &view, &registry, colorFormat, frameIndex, viewIndex, drawViewProj, prevViewProj, jitter, prevJitter, cluster, shadow](rhi::CommandEncoder& enc, Array<rhi::RenderBundle*>& out) {
                ResolveAndEmit(view, registry, enc, frameIndex, viewIndex, colorFormat, drawViewProj, prevViewProj, jitter, prevJitter, /*transparentPass*/ true, cluster, shadow, out);
            });
        });
    }

private:
    // The bundle-pass body: resolve the view's draws (single-threaded) then emit them into render
    // bundle(s) appended to `out` — serially below the threshold, else fanned out across the job
    // system (per-worker bundles). The graph replays `out` via ExecuteBundles.
    void ResolveAndEmit(const RenderView& view, const RendererRegistry& registry,
                        rhi::CommandEncoder& encoder, u32 frameIndex, u32 viewIndex, rhi::TextureFormat colorFormat,
                        const Mat4& drawViewProj, const Mat4& prevViewProj, Vec2 jitter, Vec2 prevJitter, bool transparentPass,
                        const ClusterBinding& cluster, const ShadowBinding& shadow, Array<rhi::RenderBundle*>& out,
                        bool probesEnabled = true) {
        RenderRecordContext ctx{};
        ctx.view        = &view;
        ctx.viewProj    = drawViewProj;   // opaque = jittered (TAA), transparent = unjittered (drawn post-TAA)
        ctx.prevViewProj = prevViewProj;
        ctx.jitter      = jitter;
        ctx.prevJitter  = prevJitter;
        ctx.viewMatrix  = view.Camera().view;
        ctx.cameraPos   = view.Camera().position;
        ctx.ambient     = (view.Scene() != nullptr) ? view.Scene()->Ambient() : Vec3{ 0.03f, 0.03f, 0.03f };
        ctx.cascades    = shadow.cascades;            // this view's CSM cascades
        ctx.cascadeLayerBase = shadow.layerBase;      // this view's first shadow-array layer
        ctx.lights      = (view.Scene() != nullptr) ? view.Scene()->Lights() : Span<const GpuLight>{};
        ctx.cluster     = cluster;
        ctx.frameIndex  = frameIndex;
        ctx.viewIndex   = viewIndex;
        ctx.colorFormat = colorFormat;
        ctx.depthFormat = m_depthFormat;
        ctx.probesEnabled = probesEnabled;

        // RESOLVE (single-threaded): sorted draw list -> ResolvedDraws (PSO build, mesh upload,
        // ring allocation). Split by the category's pass affinity (which pass draws it), then walk
        // runs of the SAME renderer within this pass and hand each to its owner (dispatched by
        // rendererId, not category — so blended meshes + sprites interleave by depth yet each run
        // still batches within one renderer). The list is category-sorted, so this-pass items are
        // contiguous; within the blended span, depth order mixes renderers as needed.
        const auto inThisPass = [&](const DrawItem& it) noexcept {
            const PassAffinity a = Categories().Affinity(it.data->category);
            if (a == PassAffinity::None) { return false; }
            return (a == PassAffinity::Blended) == transparentPass;
        };
        m_resolved.Clear();
        const Span<const DrawItem> items = view.DrawList();
        usize i = 0;
        while (i < items.Size()) {
            if (!inThisPass(items[i])) { ++i; continue; }
            const u16 rid = items[i].data->rendererId;
            usize j = i + 1;
            while (j < items.Size() && inThisPass(items[j]) && items[j].data->rendererId == rid) { ++j; }
            if (Renderer* r = registry.ById(rid)) {
                r->Resolve(ctx, Span<const DrawItem>{ items.Data() + i, j - i }, m_resolved);
            }
            i = j;
        }

        // EMIT into bundles. The bundle records its viewport up front (Vulkan secondaries / DX12
        // bundles can't inherit it) — this view's sub-rect of the target, not the full target.
        rhi::RenderBundleDesc bd{};
        bd.colorFormats[0]    = colorFormat;
        if (transparentPass) {
            bd.colorFormatCount = 1;                 // color-only pass
        } else {
            bd.colorFormats[1]  = kGNormalFormat;    // MRT: view-space normal
            bd.colorFormats[2]  = kGVelocityFormat;  // MRT: motion vector
            bd.colorFormatCount = 3;
        }
        bd.depthStencilFormat = m_depthFormat;
        bd.sampleCount        = 1;
        bd.viewportX          = view.ViewportX();
        bd.viewportY          = view.ViewportY();
        bd.width              = view.ViewportWidth();
        bd.height             = view.ViewportHeight();
        bd.label              = u8"forward.bundle";

        m_bundles.Clear();
        const u32 total = static_cast<u32>(m_resolved.Size());
        if (HasGlobalJobSystem() && total >= kParallelEmitThreshold) {
            EmitParallel(bd, frameIndex);
        } else if (rhi::RenderBundleEncoder* be = encoder.CreateRenderBundleEncoder(bd)) {
            for (const ResolvedDraw& d : m_resolved) { EmitDraw(*be, d); }
            m_bundles.PushBack(be->Finish());
        }
        for (rhi::RenderBundle* b : m_bundles) { if (b != nullptr) { out.PushBack(b); } }
    }

    // Split the resolved draws into <= SlotCount contiguous chunks; record each into its own
    // bundle on a JobSystem worker, using that chunk's OWN command pool (so no two threads touch
    // a pool concurrently — pools are indexed by chunk, not worker slot). Bundles are kept in
    // draw order in m_bundles. Pools were reset for this frame by BeginFrame.
    void EmitParallel(const rhi::RenderBundleDesc& bd, u32 frameIndex) {
        JobSystem& jobs = GlobalJobs();
        const u32 slots = jobs.SlotCount();
        if (!EnsureWorkerPools(slots) || m_workerSlots == 0) { return; }

        const u32 total  = static_cast<u32>(m_resolved.Size());
        const u32 grain  = (total + slots - 1u) / slots;                 // ~slots chunks
        const u32 chunks = (grain > 0) ? ((total + grain - 1u) / grain) : 1u;   // <= slots

        m_bundles.Resize(chunks);
        const u32 base = frameIndex * m_workerSlots;
        const ResolvedDraw* draws = m_resolved.Data();
        jobs.ParallelFor(chunks, [&, draws, total, grain, base](u32 c) {
            m_bundles[c] = nullptr;
            rhi::CommandEncoder* enc = m_workerEncoders[base + c];       // unique pool per chunk c
            if (enc == nullptr) { return; }
            rhi::RenderBundleEncoder* be = enc->CreateRenderBundleEncoder(bd);
            if (be == nullptr) { return; }
            const u32 begin = c * grain;
            const u32 end   = Min((c + 1u) * grain, total);
            for (u32 k = begin; k < end; ++k) { EmitDraw(*be, draws[k]); }
            m_bundles[c] = be->Finish();
        });
    }

    // (Re)provision the per-(frameIndex, slot) command-pool grid + one persistent encoder each
    // (the encoder is only a handle to its pool for CreateRenderBundleEncoder; its primary buffer
    // is never recorded/submitted, so it is created once and reused — only the pool resets). Grows
    // only. Returns false on failure (parallel emit then skips).
    bool EnsureWorkerPools(u32 slotCount) {
        if (slotCount <= m_workerSlots) { return m_workerSlots > 0; }
        ReleaseWorkerPools();
        const usize n = static_cast<usize>(m_framesInFlight) * slotCount;
        m_workerPools.Resize(n, nullptr);
        m_workerEncoders.Resize(n, nullptr);
        for (usize i = 0; i < n; ++i) {
            rhi::CommandPool* pool = nullptr;
            if (!m_device->CreateCommandPool(rhi::QueueType::Graphics, pool).IsOk() || pool == nullptr) {
                ReleaseWorkerPools(); m_workerSlots = 0; return false;
            }
            m_workerPools[i] = pool;
            rhi::CommandEncoder* enc = nullptr;
            (void)pool->CreateEncoder(enc);
            m_workerEncoders[i] = enc;
        }
        m_workerSlots = slotCount;
        return true;
    }

    void ReleaseWorkerPools() {
        for (usize i = 0; i < m_workerPools.Size(); ++i) {
            if (m_workerEncoders[i] != nullptr && m_workerPools[i] != nullptr) {
                m_workerPools[i]->DestroyEncoder(m_workerEncoders[i]);
            }
            if (m_workerPools[i] != nullptr) { m_device->DestroyCommandPool(m_workerPools[i]); }
        }
        m_workerPools.Clear();
        m_workerEncoders.Clear();
        m_workerSlots = 0;
    }

    // Above this many resolved draws, emission fans out across the job system; below it, one
    // bundle on the calling thread. (Parallel recording pays off only with many distinct draws —
    // instanced batches collapse to one resolved draw each.)
    static constexpr u32 kParallelEmitThreshold = 256;

    rhi::Device*       m_device;
    u32                m_framesInFlight = 2;
    rhi::TextureFormat m_depthFormat = rhi::TextureFormat::Depth32Float;   // depth texture is a graph transient
    Array<ResolvedDraw>       m_resolved;     // reused resolve buffer (drained each pass)
    Array<rhi::RenderBundle*> m_bundles;      // per-chunk bundles (draw order)
    // Per-(frameIndex, slot) worker command pools + persistent encoders for parallel emit.
    Array<rhi::CommandPool*>    m_workerPools;
    Array<rhi::CommandEncoder*> m_workerEncoders;
    u32                         m_workerSlots = 0;
};

// A 90 degrees-FOV camera looking along one cube face (+X,-X,+Y,-Y,+Z,-Z) from a probe center, for reflection-
// probe capture. Uses LookAtRH (right-handed => correct triangle winding, so back-face culling works).
// RH LookAt produces HORIZONTALLY-MIRRORED faces vs the cube sampler; that mirror is corrected in image
// space by a horizontal-flip blit (captured -> prefiltered), NOT in the camera — negating a camera axis
// would flip winding and break culling. Forwards/ups match Sedulous's probe + point-shadow convention.
[[nodiscard]] inline ViewCamera ProbeFaceCamera(Vec3 center, u32 face, f32 nearZ, f32 farZ) {
    static const Vec3 dirs[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    static const Vec3 ups[6]  = { {0,1,0}, {0,1,0}, {0,0,-1}, {0,0,1}, {0,1,0}, {0,1,0} };
    ViewCamera vc;
    vc.view       = Mat4::LookAtRH(center, center + dirs[face], ups[face]);
    vc.projection = Mat4::PerspectiveFovRH(1.57079633f, 1.0f, nearZ, farZ);   // 90° square
    vc.position   = center;
    vc.farZ       = farZ;
    return vc;
}

// The single per-frame driver. Begin resets shared per-frame state; AddView collects a view
// (extracting its draw list from a scene snapshot); End sizes the renderers' transient once
// for the whole frame and composes every view. One driver, all views — no per-view object.
class RenderFrame {
public:
    RenderFrame(rhi::Device& device, RendererRegistry& registry, u32 framesInFlight,
                ClusterSystem* clusters = nullptr, TonemapPass* tonemap = nullptr,
                ShadowSystem* shadows = nullptr, IBLSystem* ibl = nullptr, SkyPass* sky = nullptr,
                BloomPass* bloom = nullptr, TaaPass* taa = nullptr, AoPass* ao = nullptr, FxaaPass* fxaa = nullptr) noexcept
        : m_registry(&registry), m_pass(device, framesInFlight), m_graph(&device),
          m_clusters(clusters), m_tonemap(tonemap), m_shadows(shadows), m_ibl(ibl), m_sky(sky), m_bloom(bloom), m_taa(taa), m_ao(ao), m_fxaa(fxaa) {}

    // Begin a frame against the caller's encoder (the caller owns the encoder + targets).
    void Begin(rhi::CommandEncoder& encoder, u32 frameIndex) {
        m_encoder    = &encoder;
        m_frameIndex = frameIndex;
        m_views.Begin();
        m_graph.BeginFrame(static_cast<i32>(frameIndex));   // one graph composes all this frame's views
    }

    // Collect a view over `scene`. Builds its sorted draw list now (parallelizable later);
    // the GPU recording is deferred to End so transient buffers are sized once per frame.
    RenderView* AddView(const ExtractedScene& scene, const ViewCamera& camera, const ViewSettings& settings,
                        rhi::TextureView* target, rhi::TextureFormat targetFormat, u32 width, u32 height,
                        const void* debugScene = nullptr) {
        RenderView* view = m_views.Acquire();
        view->Bind(scene, camera, settings, target, targetFormat, width, height);
        view->SetDebugScene(debugScene);
        view->BuildDrawList(m_sortScratch);
        return view;
    }

    // Per-frame debug draw: the pass + the GLOBAL list (drawn in every view) + the SCREEN list (drawn
    // once, whole-window). Per-scene lists ride on each RenderView.
    void SetDebug(DebugDrawPass* pass, const debug::DebugDraw* global, const debug::DebugDraw* screen) noexcept {
        m_debugPass = pass; m_debugGlobal = global; m_debugScreen = screen;
    }

    // Per-frame decal pass (borrowed); null = no decals. Declared per view after sky, before AO.
    void SetDecal(DecalPass* pass) noexcept { m_decalPass = pass; }

    // Reflection-probe system (borrowed); null = no probes. Dirty probes are captured (6 faces each,
    // lit forward + sky into their cube-array slices) before the main views, so the forward can sample
    // the prefiltered result. Set once per frame before End.
    void SetProbes(ReflectionProbeSystem* probes) noexcept { m_probeSystem = probes; }

    // Turn on per-pass GPU timestamp profiling for the frame graph (idempotent).
    void EnableGpuProfiling() { m_graph.EnableGpuProfiling(); }

    // Linear exposure multiplier applied in the tonemap pass (scene/camera setting).
    void SetExposure(f32 exposure) noexcept { m_exposure = exposure; }
    // Bloom composite strength + soft-knee prefilter (intensity 0 = off).
    void SetBloom(f32 intensity, f32 threshold, f32 knee) noexcept { m_bloomIntensity = intensity; m_bloomThreshold = threshold; m_bloomKnee = knee; }
    // Temporal AA: jitters the projection + resolves against per-view history (off = no jitter, no resolve).
    // blend = max history weight (stability), gamma = variance-clip box half-width, motionScale = how fast
    // history drops with motion.
    void SetTaa(bool on, f32 blend, f32 gamma, f32 motionScale) noexcept {
        m_taaEnabled = on; m_taaBlend = blend; m_taaGamma = gamma; m_taaMotionScale = motionScale;
    }
    // Ambient occlusion: mode (Off/GTAO/SSAO) + knobs. AO is applied to the HDR before TAA (strength 0
    // or Off = no AO). debugMode != 0 forces the AO on and shows the debug channel straight to screen.
    void SetAo(AoMode mode, f32 strength, f32 radius, f32 intensity, i32 debugMode = 0) noexcept {
        m_aoMode = mode; m_aoStrength = strength; m_aoRadius = radius; m_aoIntensity = intensity; m_aoDebug = debugMode;
    }
    // FXAA (TAA-off fallback): run FXAA after tonemap when TAA is off. Never stacked with TAA.
    void SetFxaa(bool on, f32 subpixelQuality) noexcept { m_fxaaEnabled = on; m_fxaaSubpixel = subpixelQuality; }
    // Append a per-pass GPU timing report (call only after the device is idle).
    void ReadGpuProfile(String& out) {
        if (auto* p = m_graph.GpuProfiler()) { p->ReadResults(m_graph.LastProfiledPassCount(), out); }
    }

    // Depth prepass body: emit the view's OPAQUE draws as depth-only from the camera POV (no bias, so
    // the depth equals the forward pass's exactly -> LessEqual early-Z). Masked isn't prepassed (the
    // depth-only shader can't alpha-discard); transparent doesn't write depth. Runs at graph execute.
    void RecordDepthPrepass(rhi::RenderPassEncoder& rp, const RenderView& view,
                            const RendererRegistry& registry, u32 viewIndex) {
        RenderRecordContext ctx{};
        ctx.viewProj     = view.Camera().ViewProjection();
        ctx.depthFormat  = m_pass.DepthFormat();
        ctx.depthPrepass = true;
        ctx.frameIndex   = m_frameIndex;
        ctx.viewIndex    = viewIndex;

        m_prepassResolved.Clear();
        const Span<const DrawItem> items = view.DrawList();
        usize i = 0;
        while (i < items.Size()) {
            const RenderCategory cat = items[i].data->category;
            usize j = i + 1;
            while (j < items.Size() && items[j].data->category == cat) { ++j; }
            if (cat == RenderCategories::Opaque) {
                if (Renderer* r = registry.ById(items[i].data->rendererId)) {
                    r->ResolveDepthOnly(ctx, Span<const DrawItem>{ items.Data() + i, j - i }, m_prepassResolved);
                }
            }
            i = j;
        }
        for (const ResolvedDraw& d : m_prepassResolved) { EmitDraw(rp, d); }
    }

    // Resolve + emit the scene's casters as depth-only draws from the light's POV (the shadow depth
    // pass body). lightViewProj is the depth shader's "camera". Runs at graph execute time, before
    // the forward pass (which ReadTextures the shadow map), so it fills the rings ahead of forward.
    // Re-emit a caster list as depth-only draws from a light's POV. `casters` is camera-independent for
    // local lights (the scene-global list) or the view's draw list for cascades. When cullRadius > 0,
    // casters whose world bounding sphere doesn't intersect the light sphere (cullCenter, cullRadius)
    // are skipped — per-light shadow-caster culling (phase 5.4).
    void RecordShadowCasters(rhi::RenderPassEncoder& rp, Span<const DrawItem> casters,
                             const RendererRegistry& registry, const Mat4& lightViewProj,
                             Vec3 cullCenter = {}, f32 cullRadius = 0.0f) {
        RenderRecordContext ctx{};
        ctx.viewProj    = lightViewProj;
        ctx.depthFormat = (m_shadows != nullptr) ? m_shadows->Format() : rhi::TextureFormat::Depth32Float;
        ctx.frameIndex  = m_frameIndex;
        ctx.viewIndex   = 0;

        // Optional sphere cull into a scratch list (keeps the category-run batching below intact).
        Span<const DrawItem> items = casters;
        if (cullRadius > 0.0f) {
            m_shadowCullScratch.Clear();
            for (const DrawItem& it : casters) {
                const auto* md = static_cast<const MeshRenderData*>(it.data);
                const Vec3  d  = md->worldCenter - cullCenter;
                const f32   r  = cullRadius + md->worldRadius;
                if (Dot(d, d) <= r * r) { m_shadowCullScratch.PushBack(it); }
            }
            items = Span<const DrawItem>{ m_shadowCullScratch.Data(), m_shadowCullScratch.Size() };
        }

        // Group by RENDERER (not category): the caster list is the view's draw list, whose blended span
        // now mixes transparent meshes (id 0) and sprites (id 1) interleaved by depth. Dispatching a
        // whole category run to the first item's renderer would hand sprite data to the mesh renderer
        // (read as MeshRenderData -> garbage/UAF). Same-renderer runs route correctly; sprites' depth-only
        // resolve is a no-op (they don't cast shadows).
        m_shadowResolved.Clear();
        usize i = 0;
        while (i < items.Size()) {
            const u16 rid = items[i].data->rendererId;
            usize j = i + 1;
            while (j < items.Size() && items[j].data->rendererId == rid) { ++j; }
            if (Renderer* r = registry.ById(rid)) {
                r->ResolveDepthOnly(ctx, Span<const DrawItem>{ items.Data() + i, j - i }, m_shadowResolved);
            }
            i = j;
        }
        for (const ResolvedDraw& d : m_shadowResolved) { EmitDraw(rp, d); }
    }

    // Build the camera-independent shadow-caster list from a scene (opaque + masked meshes), grouped by
    // (mesh, material) so the depth pass batches them. Used by local-light shadows so their atlas tiles
    // are stable across camera motion (required for static caching) and include off-camera casters.
    void BuildShadowCasterList(const ExtractedScene& scene) {
        m_shadowCasters.Clear();
        m_animatedSpheres.Clear();
        for (RenderData* data : scene.Items()) {
            if (data == nullptr) { continue; }
            if (data->category != RenderCategories::Opaque && data->category != RenderCategories::Masked) { continue; }
            const auto* md = static_cast<const MeshRenderData*>(data);
            // Animated (skinned) casters deform every frame: remember each one's world bounding sphere so
            // only the static atlas tiles whose light volume it overlaps get re-rendered (per-tile routing).
            if (md->boneMatrices != nullptr && md->boneCount > 0) {
                m_animatedSpheres.PushBack(Sphere{ md->worldCenter, md->worldRadius });
            }
            const usize m = reinterpret_cast<usize>(md->mesh), n = reinterpret_cast<usize>(md->material);
            const u32 stateBits = static_cast<u32>((((m >> 4) * 1099511628211ull + (n >> 4)) & ((1u << kSortStateBits) - 1)));
            m_shadowCasters.PushBack(DrawItem{ MakeSortKey(data->category, stateBits, 0u), data });
        }
        RadixSortDrawItems(m_shadowCasters, m_sortScratch);
    }

    // A signature over the STATIC local casters' transforms (quantized) + count. When it changes, the
    // cached static atlas layer is re-rendered for one frames-in-flight cycle. The Static-mode contract
    // is that caster GEOMETRY doesn't move, so only the lights themselves feed the signature.
    [[nodiscard]] u64 StaticCasterSignature(const RenderView* primary) const {
        if (primary == nullptr || primary->Scene() == nullptr) { return 0; }
        u64 sig = 1469598103934665603ull;   // FNV-1a offset basis
        const auto mix = [&sig](f32 v) {
            const u64 q = static_cast<u64>(static_cast<i64>(v * 1000.0f));   // ~1mm / 0.001 quantization
            sig = (sig ^ q) * 1099511628211ull;
        };
        for (const LocalShadowCaster& c : primary->Scene()->LocalShadowCasters()) {
            if (!c.isStatic) { continue; }
            mix(static_cast<f32>(c.type));
            mix(c.positionWS.x); mix(c.positionWS.y); mix(c.positionWS.z);
            mix(c.directionWS.x); mix(c.directionWS.y); mix(c.directionWS.z);
            mix(c.range); mix(c.outerAngle);
        }
        return sig;
    }

    // Compose all collected views into the frame's encoder.
    void End() {
        if (m_encoder == nullptr) { return; }

        u32 totalDraws = 0;
        for (usize i = 0; i < m_views.ActiveCount(); ++i) {
            totalDraws += static_cast<u32>(m_views.At(i)->DrawList().Size());
        }

        // The directional shadow map is shared by all views this frame (one caster in 5.1). Determine
        // it from the first view's scene + ensure the texture BEFORE the renderers build set 0 (which
        // binds the map). Renderers get the real map when a caster exists, else null -> their dummy.
        const RenderView* primary = (m_views.ActiveCount() > 0) ? m_views.At(0) : nullptr;
        const bool hasShadow = m_shadows != nullptr && primary != nullptr && primary->Scene() != nullptr
                            && primary->Scene()->DirectionalShadowData().valid;
        // One shadow ARRAY shared by all views, sized for per-view cascades (viewCount * cascades
        // layers). Each view fits + renders its OWN cascades into its layer range, and samples them.
        const u32 viewCount = static_cast<u32>(m_views.ActiveCount());
        rhi::TextureView* shadowMap = hasShadow ? m_shadows->PrepareFrame(m_frameIndex, viewCount) : nullptr;
        const u64 shadowGen = (m_shadows != nullptr) ? m_shadows->Generation() : 0;

        // Local-light (spot) shadows (5.3): build the per-caster perspective matrices + atlas tiles up
        // front, scene-global (one atlas shared by all views). Doing it here lets the renderers size
        // their per-object rings (SetShadowAtlas passCount) and upload the data before the forward.
        m_localShadows.Clear();
        m_rtTiles.Clear();
        m_staticTiles.Clear();
        rhi::TextureView* atlasView = nullptr;
        if (m_shadows != nullptr && primary != nullptr && primary->Scene() != nullptr) {
            const Span<const LocalShadowCaster> casters = primary->Scene()->LocalShadowCasters();
            const u32 capacity = m_shadows->AtlasTileCapacity();   // per layer
            if (!casters.IsEmpty()) { atlasView = m_shadows->PrepareAtlas(m_frameIndex); }
            if (atlasView != nullptr) {
                BuildShadowCasterList(*primary->Scene());   // camera-independent casters for the tiles
                const u32 atlasRes = m_shadows->AtlasResolution();
                const u32 tileRes  = m_shadows->AtlasTileResolution();
                // Each layer (realtime / static) has its own tile space; the running per-layer tile base
                // must match the shadowIndex extraction assigned. m_localShadows stays in CASTER order
                // (so shadowIndex indexes it), each entry tagged with its layer via atlasSelect.
                u32 rtTile = 0, stTile = 0;
                for (usize i = 0; i < casters.Size(); ++i) {
                    const LocalShadowCaster& c = casters[i];
                    const u32 need = (c.type == 1u /*point*/) ? 6u : 1u;
                    u32& tileCtr = c.isStatic ? stTile : rtTile;
                    if (tileCtr + need > capacity) { continue; }   // matches extraction's per-layer cap
                    Array<LocalShadowTile>& dst = c.isStatic ? m_staticTiles : m_rtTiles;
                    for (u32 f = 0; f < need; ++f) {
                        const u32 ti = tileCtr + f;   // tile index WITHIN the layer
                        GpuLocalShadow s = (need == 6u) ? BuildPointShadowFace(c, f, ti, atlasRes, tileRes)
                                                        : BuildSpotShadow(c, ti, atlasRes, tileRes);
                        s.atlasSelect = c.isStatic ? 1.0f : 0.0f;   // sampled atlas array layer
                        const AtlasTile t = AtlasTileRect(ti, atlasRes, tileRes);
                        // Cull casters to the light's bounding sphere (point/spot share pos + range).
                        dst.PushBack(LocalShadowTile{ s.viewProj, t.x, t.y, t.w, t.h,
                                                      c.positionWS, Max(0.1f, c.range) });
                        m_localShadows.PushBack(s);
                    }
                    tileCtr += need;
                }
            }
        }
        const u64 atlasGen = (m_shadows != nullptr) ? m_shadows->Generation() : 0;

        // Static atlas layer: each tile is cached and re-rendered only when needed, tracked by a
        // per-tile dirty COUNTDOWN (a tile must re-render for FramesInFlight frames to refresh every
        // in-flight slot's copy). Two things dirty a tile:
        //   1. The static caster set changed (signature trip) — dirty ALL tiles.
        //   2. An animated caster's world sphere overlaps the tile's light volume — dirty THAT tile,
        //      every frame it overlaps (skinned casters deform per frame; node bounds don't move, so
        //      the signature never trips for them). This is the per-caster routing: only tiles actually
        //      containing animation re-render; tiles with purely static geometry stay cached.
        const u32 fif = (m_shadows != nullptr) ? m_shadows->FramesInFlight() : 1u;
        m_staticTileDirty.Resize(m_staticTiles.Size());   // index-stable: static caster set is stable by contract
        const u64 staticSig = StaticCasterSignature(primary);
        if (staticSig != m_staticSig) {
            m_staticSig = staticSig;
            for (u32& d : m_staticTileDirty) { d = fif; }
        }
        for (usize ti = 0; ti < m_staticTiles.Size(); ++ti) {
            const LocalShadowTile& t = m_staticTiles[ti];
            for (const Sphere& s : m_animatedSpheres) {
                if (Length(s.center - t.cullCenter) <= t.cullRadius + s.radius) { m_staticTileDirty[ti] = fif; break; }
            }
        }
        // Collect this frame's static tiles to render (countdown > 0) and tick the countdowns down.
        m_staticRenderTiles.Clear();
        for (usize ti = 0; ti < m_staticTiles.Size(); ++ti) {
            if (m_staticTileDirty[ti] > 0) { m_staticRenderTiles.PushBack(m_staticTiles[ti]); --m_staticTileDirty[ti]; }
        }
        const bool renderStatic = !m_staticRenderTiles.IsEmpty();
        // Per-renderer ring sizing: count only the atlas passes that actually re-emit casters this frame.
        const u32 localPassCount = static_cast<u32>(m_rtTiles.Size()) +
                                   static_cast<u32>(m_staticRenderTiles.Size());

        {
            DRACONIC_PROFILE_SCOPE("Compose.Prepare");   // per-frame GPU buffer sizing + pool resets
            for (Renderer* r : m_registry->Unique()) { r->SetShadowMap(shadowMap, shadowGen); }
            for (Renderer* r : m_registry->Unique()) { r->SetShadowAtlas(atlasView, atlasGen, localPassCount); }
            // Probe capture re-emits the draws once per face (P1b caps at 1 probe/frame = 6 forward passes);
            // count them into the per-object rings or the extra passes would starve the forward (silent drops).
            const u32 captureFaces = (m_probeSystem != nullptr && !m_probeSystem->Captures().IsEmpty()) ? 6u : 0u;
            for (Renderer* r : m_registry->Unique()) { r->SetCaptureFacePasses(captureFaces); }
            // Reflection probes (P4, multi-probe): upload this frame's records + bind the prefiltered cube-
            // array (t8) + the probe-metadata SRV (t9) + count. The forward loops + blends them. 0 -> no probe.
            if (m_probeSystem != nullptr && m_probeSystem->ActiveCount() > 0) {
                m_probeSystem->Upload();
                for (Renderer* r : m_registry->Unique()) {
                    r->SetProbes(m_probeSystem->PrefilterArrayView(), m_probeSystem->ProbeBuffer(), m_probeSystem->ActiveCount());
                }
            } else {
                for (Renderer* r : m_registry->Unique()) { r->SetProbes(nullptr, nullptr, 0); }
            }
            if (m_ibl != nullptr && m_ibl->Ready()) {
                m_ibl->Upload(*m_encoder);   // pending equirect/cubemap uploads, before the graph executes
                for (Renderer* r : m_registry->Unique()) {
                    r->SetIBL(m_ibl->ShBuffer(), m_ibl->PrefilterView(), m_ibl->BrdfView(), m_ibl->MaxLod(), m_ibl->Generation());
                }
            }
            for (Renderer* r : m_registry->Unique()) { r->PrepareFrame(totalDraws, m_frameIndex); }
            for (Renderer* r : m_registry->Unique()) {
                r->UploadLocalShadows(Span<const GpuLocalShadow>{ m_localShadows.Data(), m_localShadows.Size() }, m_frameIndex);
            }
            // Skinning bone upload: write each distinct skeleton instance's matrices ONCE into the bone
            // pool + copy staging->device, before any pass reads them. Scene-global (the primary scene's
            // instances cover every view of it). Must run before the graph executes (below).
            if (m_encoder != nullptr && primary != nullptr && primary->Scene() != nullptr) {
                for (Renderer* r : m_registry->Unique()) { r->UploadSkinning(*primary->Scene(), *m_encoder); }
            }
            if (m_clusters != nullptr) { m_clusters->PrepareFrame(m_frameIndex); }   // size the cluster build's per-frame buffers
            m_pass.BeginFrame(m_frameIndex);   // reset per-worker pools once (before any view)
        }

        // Declare every view's forward pass into the one frame graph, then let the graph compile
        // (barriers + transient depth allocation/aliasing) + execute. (§9: one graph, all views.)
        {
        DRACONIC_PROFILE_SCOPE("Compose.Declare");   // build the frame graph (pass/resource declarations)
        if (m_views.ActiveCount() > 0) {
            m_graph.SetOutputSize(m_views.At(0)->Width(), m_views.At(0)->Height());
        }

        // Per-view CSM: import the shared cascade array once; each view fits its own cascades to its
        // camera and renders them into its layer range (so split-screen views don't share a fit).
        rendergraph::RGHandle shadowH;
        const bool  shadowActive = hasShadow && shadowMap != nullptr;
        const Vec3  lightDir      = (hasShadow && primary != nullptr) ? primary->Scene()->DirectionalShadowData().direction : Vec3{ 0, -1, 0 };
        const u32   cascadeCount  = (m_shadows != nullptr) ? m_shadows->CascadeCount() : 4u;
        const u32   shadowRes     = (m_shadows != nullptr) ? m_shadows->Resolution() : 1024u;

        // IBL precompute: the active sky source builds into persistent products (env/SH/prefilter/BRDF)
        // when dirty; the forward pass samples them in set 0. The procedural sky tracks the directional
        // light so its sun disc + ambient match the scene's key light.
        if (m_ibl != nullptr) {
            m_ibl->SetSun(lightDir);
            if (primary != nullptr && primary->Scene() != nullptr) { m_ibl->SetSky(primary->Scene()->Sky()); }
            m_ibl->ProcessPending(m_graph);
        }

        // Local-light shadow atlas (5.3/5.4): a 2-layer array. Layer 0 (realtime) re-renders every
        // frame; layer 1 (static) only when the static set changed (renderStatic). Each pass targets
        // its layer (subresource), clears it, and renders its tiles (per-tile viewport+scissor). Every
        // forward pass ReadTextures the array, ordering both passes ahead + barriering it readable.
        rendergraph::RGHandle atlasH;
        const bool atlasActive = atlasView != nullptr && (!m_rtTiles.IsEmpty() || !m_staticTiles.IsEmpty());
        if (atlasActive) {
            atlasH = m_shadows->ImportAtlas(m_graph, m_frameIndex);
            RendererRegistry* reg  = m_registry;
            const u32 atlasRes     = m_shadows->AtlasResolution();
            // Declare one layer's depth pass over a tile list. (Lambda-per-pass; the graph runs them
            // at execute time, ordered before the forward by its ReadTexture of atlasH.)
            const auto declareLayer = [&](u32 layer, Array<LocalShadowTile>* tiles) {
                if (tiles->IsEmpty()) { return; }
                m_graph.AddRenderPass(u8"shadow.atlas", [this, atlasH, reg, tiles, atlasRes, layer](rendergraph::PassBuilder& b) {
                    rendergraph::RGSubresourceRange sub{}; sub.baseArrayLayer = layer; sub.arrayLayerCount = 1;
                    b.SetDepthTarget(atlasH, rhi::LoadOp::Clear, rhi::StoreOp::Store, /*clearDepth*/ 1.0f, sub);
                    b.SetViewport(0, 0, atlasRes, atlasRes);   // pass default; each tile sets its own below
                    b.SetExecute([this, reg, tiles](rhi::RenderPassEncoder& rp) {
                        const Span<const DrawItem> casters{ m_shadowCasters.Data(), m_shadowCasters.Size() };
                        for (const LocalShadowTile& t : *tiles) {
                            rp.SetViewport(static_cast<f32>(t.x), static_cast<f32>(t.y),
                                           static_cast<f32>(t.w), static_cast<f32>(t.h));
                            rp.SetScissor(static_cast<i32>(t.x), static_cast<i32>(t.y), t.w, t.h);
                            RecordShadowCasters(rp, casters, *reg, t.viewProj, t.cullCenter, t.cullRadius);
                        }
                    });
                });
            };
            declareLayer(0u, &m_rtTiles);                                  // realtime layer — every frame
            if (renderStatic) { declareLayer(1u, &m_staticRenderTiles); }  // static layer — only dirty tiles
        }
        if (shadowActive) { shadowH = m_shadows->ImportTarget(m_graph, m_frameIndex); }

        // Declare EVERY view's cascade depth passes up front — before any forward pass. The cascade
        // array is one imported resource: if a forward pass sampling the whole array were declared
        // before a later view's cascade writes, those layers would still be in DepthStencilAttachment
        // (not Read) when sampled (VUID-vkCmdExecuteCommands depth-layout). Fitting all cascades first
        // means every layer is written + barriered to Read before the first forward sample.
        Array<ShadowBinding> viewShadows;
        viewShadows.Resize(m_views.ActiveCount());
        if (shadowActive) {
            for (usize i = 0; i < m_views.ActiveCount(); ++i) {
                if (i >= ShadowSystem::kMaxShadowViews) { break; }
                RenderView* v = m_views.At(i);
                const f32 shadowDistance = Min(v->Camera().farZ, 150.0f);
                const ShadowCascades cascades = ComputeCascades(v->Camera(), lightDir, shadowDistance, shadowRes);
                const u32 layerBase = static_cast<u32>(i) * cascadeCount;
                const RenderView* casters = v;
                RendererRegistry* reg = m_registry;
                for (u32 c = 0; c < cascadeCount; ++c) {
                    const Mat4 cascadeVP = cascades.viewProj[c];
                    const u32  layer     = layerBase + c;
                    m_graph.AddRenderPass(u8"shadow.cascade", [this, shadowH, cascadeVP, shadowRes, layer, casters, reg](rendergraph::PassBuilder& b) {
                        rendergraph::RGSubresourceRange sub{}; sub.baseArrayLayer = layer; sub.arrayLayerCount = 1;
                        b.SetDepthTarget(shadowH, rhi::LoadOp::Clear, rhi::StoreOp::Store, /*clearDepth*/ 1.0f, sub);
                        b.SetViewport(0, 0, shadowRes, shadowRes);
                        b.SetExecute([this, cascadeVP, casters, reg](rhi::RenderPassEncoder& rp) {
                            RecordShadowCasters(rp, casters->DrawList(), *reg, cascadeVP);
                        });
                    });
                }
                ShadowBinding& sb = viewShadows[i];
                sb.sampleView = shadowMap;
                sb.handle     = shadowH;
                sb.cascades   = cascades;
                sb.layerBase  = layerBase;
                sb.valid      = true;
            }
        }

        // Reflection probe (P2): init the captured-cube layout once (so uncaptured slices are ShaderRead, not
        // UNDEFINED, under the whole-array SRV) + import it ONCE so both the capture passes AND the main
        // forward's ReadTexture share one imported resource (which orders capture->forward + barriers it).
        rendergraph::RGHandle probeCapturedH, probePrefilteredH; bool probeActive = false;
        if (m_probeSystem != nullptr && m_probeSystem->ActiveCount() > 0 && m_encoder != nullptr) {
            m_probeSystem->InitLayouts(*m_encoder);
            probeCapturedH    = m_probeSystem->ImportCaptured(m_graph);
            probePrefilteredH = m_probeSystem->ImportPrefiltered(m_graph);   // the SEPARATE texture the forward samples
            probeActive = true;
        }

        // ---- Reflection probe capture (P1b) --------------------------------------------------------
        // Render each dirty probe's 6 faces (lit forward + sky, HDR) into its captured-cube slices, BEFORE
        // the main views (which sample the result). Feedback-safe: the capture forward samples the GLOBAL
        // IBL only, never the probe array. Capped at one probe/frame for P1b.
        if (probeActive && m_ibl != nullptr && m_ibl->Ready() && primary != nullptr &&
            primary->Scene() != nullptr && !m_probeSystem->Captures().IsEmpty()) {
            const rendergraph::RGHandle capturedH = probeCapturedH;
            IblBinding capIbl;
            capIbl.prefilterHandle = m_ibl->PrefilterHandle();
            capIbl.brdfHandle      = m_ibl->BrdfHandle();
            capIbl.shHandle        = m_ibl->ShHandle();
            capIbl.valid           = true;
            // Reuse the primary view's CSM binding (handle + cascades) + the local atlas. The capture
            // forward's shader statically samples both depth textures, so the binding MUST be valid or the
            // graph won't barrier them to DEPTH_STENCIL_READ_ONLY (they'd still be in ATTACHMENT layout).
            // The cascades are geometrically the primary camera's (approximate for a face) — fine for P1b.
            ShadowBinding capShadow = viewShadows.IsEmpty() ? ShadowBinding{} : viewShadows[0];
            capShadow.atlasHandle = atlasH;
            capShadow.atlasValid  = atlasActive;
            const u32 res   = ReflectionProbeSystem::kCaptureRes;
            const f32 nearZ = ReflectionProbeSystem::kCaptureNear;
            const f32 farZ  = ReflectionProbeSystem::kCaptureFar;
            const ReflectionProbeSystem::CaptureTask task = m_probeSystem->Captures()[0];
            const u32 layerBase = ReflectionProbeSystem::LayerBase(task.slot);
            const f32 sunInt = m_ibl->HasSunDisc() ? m_ibl->SunIntensity() : 0.0f;
            for (u32 face = 0; face < 6; ++face) {
                const ViewCamera fc = ProbeFaceCamera(task.center, face, nearZ, farZ);
                RenderView& cv = m_captureViews[face];
                cv.Bind(*primary->Scene(), fc, ViewSettings{}, nullptr, ReflectionProbeSystem::kCubeFormat, res, res);
                cv.BuildDrawList(m_sortScratch);

                const rendergraph::RGHandle capDepth = m_graph.CreateTransient(
                    u8"probe.depth", rendergraph::RGTextureDesc(m_pass.DepthFormat(), res, res));
                const rendergraph::RGHandle capNormal = m_graph.CreateTransient(
                    u8"probe.normal", rendergraph::RGTextureDesc(kGNormalFormat, res, res));
                const rendergraph::RGHandle capVel = m_graph.CreateTransient(
                    u8"probe.velocity", rendergraph::RGTextureDesc(kGVelocityFormat, res, res));

                rendergraph::RGSubresourceRange sub{};
                sub.baseArrayLayer = layerBase + face; sub.arrayLayerCount = 1;

                const Mat4 faceVP = fc.ViewProjection();
                // Lit forward: cluster {} -> dummy cluster -> all-lights fallback (no per-face build);
                // clear depth (no prepass); write only color slot 0 into this cube face.
                m_pass.DeclarePass(cv, *m_registry, m_graph, m_frameIndex, /*viewIndex*/ 0u,
                                   capturedH, capDepth, /*clearColor*/ true, ReflectionProbeSystem::kCubeFormat,
                                   capNormal, capVel, faceVP, Vec2{ 0, 0 }, Vec2{ 0, 0 },
                                   ClusterBinding{}, capShadow, capIbl, rhi::LoadOp::Clear, sub);
                // Sky into the same face, after the forward (loads the captured depth).
                // Distinct sky uniform slot per capture face (2..7), so the capture never shares SkyPass's
                // per-view slot with a main view (0,1) or with another face — otherwise the last recorder
                // wins the shared slot and the captured sky reads a main view's camera (cross-view leak).
                m_sky->DeclareSky(m_graph, capturedH, capVel, capDepth, m_ibl->EnvHandle(), m_ibl->EnvView(),
                                  ReflectionProbeSystem::kCubeFormat, m_pass.DepthFormat(),
                                  Inverse(faceVP), faceVP, Vec2{ 0, 0 }, Vec2{ 0, 0 },
                                  task.center, m_ibl->SkyIntensity(),
                                  m_ibl->SunDir(), m_ibl->SunAngularSize(), Vec3{ 1.0f, 0.98f, 0.92f }, sunInt,
                                  0, 0, res, res, m_frameIndex, /*viewIndex*/ 2u + face, sub);
            }
            // Bridge captured -> prefiltered mip 0 (flip blit — corrects the RH-LookAt mirror) so the forward
            // samples a SEPARATE texture, never the captured cube it just wrote; then GGX-convolve mip 0 into
            // the rougher mips (roughness reflections).
            m_probeSystem->DeclareBlit(m_graph, capturedH, probePrefilteredH, task.slot);
            m_probeSystem->DeclarePrefilter(m_graph, probePrefilteredH, task.slot);
            m_probeSystem->MarkCaptured(task.slot);
        }

        // Import each distinct target ONCE (so the graph orders/barriers all views writing it as one
        // resource). The first view to a target clears it; later views into the same target Load,
        // preserving earlier views' regions (split-screen). Targets are few — a linear scan is fine.
        struct TargetImport { rhi::TextureView* target; rendergraph::RGHandle handle; u32 w; u32 h; rhi::TextureFormat fmt; };
        Array<TargetImport> imported;
        // Bracket the decal ring ONCE for the whole per-view loop (each view accumulates its own slots).
        if (m_decalPass != nullptr) { m_decalPass->BeginFrame(m_frameIndex); }
        for (usize i = 0; i < m_views.ActiveCount(); ++i) {
            RenderView* v = m_views.At(i);
            rhi::TextureView* tgt = v->Target();
            if (tgt == nullptr) { continue; }

            rendergraph::RGHandle colorH;
            bool found = false;
            for (const TargetImport& ti : imported) { if (ti.target == tgt) { colorH = ti.handle; found = true; break; } }
            if (!found) {
                // Backbuffer (targetTexture null): current==final==RenderTarget — the host did
                // Undefined->RenderTarget and will Present, so the graph touches no barrier. Offscreen
                // (targetTexture set): the graph barriers it current -> RenderTarget -> final (e.g.
                // ShaderRead/CopySrc so the caller can sample/blit the result).
                const ViewSettings& s = v->Settings();
                colorH = m_graph.ImportTarget(u8"forward.color", s.targetTexture, tgt,
                                              s.targetFinalState, s.targetCurrentState);
                imported.PushBack(TargetImport{ tgt, colorH, v->Width(), v->Height(), v->TargetFormat() });
            }
            const bool clearColor = !found;   // first view to a target clears it; later views Load

            // Cluster build (compute) declared before the forward pass so the graph orders the
            // light-binning write ahead of the shading read. viewIndex isolates per-view buffers.
            const u32 viewIndex = static_cast<u32>(i);
            ClusterBinding cluster;
            if (m_clusters != nullptr) { cluster = m_clusters->DeclareBuild(m_graph, *v, m_frameIndex, viewIndex); }

            // This view's CSM cascades (fit + declared up front, above). Cascades stay on the view's
            // camera-culled draw list — they're already camera-coupled (refit per frame).
            ShadowBinding shadow = viewShadows[i];
            // The local-light atlas is scene-global (one pass for all views) — every view depends on it.
            shadow.atlasHandle = atlasH;
            shadow.atlasValid  = atlasActive;

            // IBL products (scene-global) for the forward to sample + barrier-order this frame.
            IblBinding ibl;
            if (m_ibl != nullptr && m_ibl->Ready()) {
                ibl.prefilterHandle = m_ibl->PrefilterHandle();
                ibl.brdfHandle      = m_ibl->BrdfHandle();
                ibl.shHandle        = m_ibl->ShHandle();
                ibl.valid           = true;
            }

            // Per-view depth, shared by the forward pass + the sky pass (sky depth-tests against it).
            const rendergraph::RGHandle depth = m_graph.CreateTransient(
                u8"forward.depth", rendergraph::RGTextureDesc(m_pass.DepthFormat(), v->Width(), v->Height()));
            // Per-view MRT G-buffer aux targets (view-space normal + motion vector) — written by the
            // forward pass, consumed by the post stack (TAA/GTAO). Unused this phase; transients free after.
            const rendergraph::RGHandle normalT = m_graph.CreateTransient(
                u8"forward.normal", rendergraph::RGTextureDesc(kGNormalFormat, v->Width(), v->Height()));
            const rendergraph::RGHandle velocityT = m_graph.CreateTransient(
                u8"forward.velocity", rendergraph::RGTextureDesc(kGVelocityFormat, v->Width(), v->Height()));

            // TAA jitter: sub-pixel-offset the projection so the resolve accumulates supersamples. Applied
            // BEFORE reading the view-proj, so the prepass + forward + sky all use the SAME jittered matrix
            // (mismatched depth would break the prepass early-Z). Off when TAA is disabled.
            const Mat4 unjitteredVP = v->Camera().ViewProjection();   // captured BEFORE jitter (transparent draws with this, post-TAA)
            Vec2 jitter{ 0.0f, 0.0f };
            if (m_taaEnabled && m_taa != nullptr) {
                jitter = HaltonJitter(m_jitterIndex, v->Width(), v->Height());
                v->ApplyProjectionJitter(jitter.x, jitter.y);
            }
            // Motion vectors: this view's previous-frame (jittered) view-proj + jitter (no motion on first
            // sight). Record this frame's for next frame.
            const Mat4 curViewProj = v->Camera().ViewProjection();   // jittered when TAA on
            const Mat4 prevViewProj = (viewIndex < m_prevViewProj.Size()) ? m_prevViewProj[viewIndex] : curViewProj;
            if (m_curViewProj.Size() <= viewIndex) { m_curViewProj.Resize(viewIndex + 1u, curViewProj); }
            m_curViewProj[viewIndex] = curViewProj;
            const Vec2 prevJitter = (viewIndex < m_prevJitter.Size()) ? m_prevJitter[viewIndex] : Vec2{ 0.0f, 0.0f };
            if (m_curJitter.Size() <= viewIndex) { m_curJitter.Resize(viewIndex + 1u, jitter); }
            m_curJitter[viewIndex] = jitter;

            // Depth prepass: opaque-only, clears + writes the camera depth so the forward pass shades
            // each opaque pixel once (early-Z via LessEqual). Declared before the forward, which Loads it.
            {
                RenderView* pv = v;
                RendererRegistry* reg = m_registry;
                const u32 vi = viewIndex;
                m_graph.AddRenderPass(u8"depth.prepass", [this, depth, pv, reg, vi](rendergraph::PassBuilder& b) {
                    b.SetDepthTarget(depth, rhi::LoadOp::Clear, rhi::StoreOp::Store);
                    b.SetViewport(pv->ViewportX(), pv->ViewportY(), pv->ViewportWidth(), pv->ViewportHeight());
                    b.NeverCull();
                    b.SetExecute([this, pv, reg, vi](rhi::RenderPassEncoder& rp) { RecordDepthPrepass(rp, *pv, *reg, vi); });
                });
            }

            // Declare the visible sky into `colorTarget` after the forward pass (if IBL + sky active).
            const auto declareSky = [&](rendergraph::RGHandle colorTarget, rendergraph::RGHandle velocityTarget, rhi::TextureFormat colorFmt) {
                if (m_sky == nullptr || m_ibl == nullptr || !m_ibl->Ready()) { return; }
                // Reconstruct the sky ray from the UNJITTERED view-proj: the background is at infinity, so
                // jittering its sampling buys ~no AA but makes it oscillate sub-pixel each frame — which TAA
                // can only partly cancel, i.e. the wobble. Unjittered => temporally invariant sky under a
                // static camera; the sky pass still writes a geometric motion vector so rotation reprojects.
                const Mat4 invVP  = Inverse(unjitteredVP);
                // The crisp analytic sun disc is for untextured skies; textured envs carry their own sun.
                const f32 sunInt = m_ibl->HasSunDisc() ? m_ibl->SunIntensity() : 0.0f;
                m_sky->DeclareSky(m_graph, colorTarget, velocityTarget, depth, m_ibl->EnvHandle(), m_ibl->EnvView(),
                                  colorFmt, m_pass.DepthFormat(), invVP, prevViewProj, jitter, prevJitter,
                                  v->Camera().position, m_ibl->SkyIntensity(),
                                  m_ibl->SunDir(), m_ibl->SunAngularSize(), Vec3{ 1.0f, 0.98f, 0.92f }, sunInt,
                                  v->ViewportX(), v->ViewportY(), v->ViewportWidth(), v->ViewportHeight(),
                                  m_frameIndex, viewIndex);
            };

            if (m_tonemap != nullptr) {
                // HDR path: forward renders linear HDR into a transient, then the tonemap pass
                // resolves it (exposure + tonemap + OETF) into the LDR target.
                const rendergraph::RGHandle hdr = m_graph.CreateTransient(
                    u8"forward.hdr", rendergraph::RGTextureDesc(m_tonemap->HdrFormat(), v->Width(), v->Height()));
                m_pass.DeclarePass(*v, *m_registry, m_graph, m_frameIndex, viewIndex, hdr, depth, /*clear*/ true,
                                   m_tonemap->HdrFormat(), normalT, velocityT, prevViewProj, jitter, prevJitter, cluster, shadow, ibl,
                                   rhi::LoadOp::Load, rendergraph::RGSubresourceRange{}, probePrefilteredH, probeActive);
                declareSky(hdr, velocityT, m_tonemap->HdrFormat());   // sky into HDR (+ camera-motion velocity), before TAA
                // Screen-space decals: project onto the opaque depth + blend into the lit HDR, AFTER sky
                // and BEFORE AO/TAA (so decals get TAA-resolved). Uses the jittered view-proj (matches the
                // depth). Per-scene decal list rides on the view's snapshot.
                if (m_decalPass != nullptr && v->Scene() != nullptr) {
                    m_decalPass->DeclareDecals(m_graph, hdr, depth, v->Scene()->Decals(), curViewProj,
                                               v->Width(), v->Height(),
                                               v->ViewportX(), v->ViewportY(), v->ViewportWidth(), v->ViewportHeight());
                }
                // AO (GTAO or SSAO) from the opaque depth+normal G-buffer, computed BEFORE the TAA resolve
                // and multiplied into the HDR pre-TAA, so TAA stabilizes it (applying AO post-TAA wobbles,
                // since the AO is computed from the jittered G-buffer and shifts sub-pixel each frame).
                const bool aoActive = (m_aoMode != AoMode::Off) || m_aoDebug != 0;
                const AoMode aoMode = (m_aoMode != AoMode::Off) ? m_aoMode : AoMode::GTAO;   // debug needs a generator
                rendergraph::RGHandle aoH{};
                if (m_ao != nullptr && aoActive) {
                    aoH = m_ao->DeclareAo(m_graph, depth, normalT, v->Width(), v->Height(),
                                          Inverse(v->Camera().projection), v->Camera().projection,
                                          m_aoRadius, m_aoIntensity, m_frameIndex, aoMode, m_aoDebug);
                }
                const bool showAo = aoH.IsValid() && m_aoDebug != 0;   // debug: AO/channel straight to screen
                rendergraph::RGHandle litHdr = hdr;
                if (aoH.IsValid() && m_aoMode != AoMode::Off && m_aoDebug == 0) {
                    litHdr = m_ao->DeclareApply(m_graph, hdr, aoH, v->Width(), v->Height(), m_aoStrength);
                }
                // TAA resolve on the opaque+sky+AO HDR (jittered) -> stable HDR. Then transparent composites
                // on the RESOLVED image (see below), so it's never temporally accumulated (no ghost) or
                // jittered (no wobble). Bloom + tonemap run on the resolved color.
                rendergraph::RGHandle sceneColor = litHdr;
                if (m_taaEnabled && m_taa != nullptr) {
                    const f32 taaFar  = (v->Camera().farZ > 0.0f) ? v->Camera().farZ : 1000.0f;
                    sceneColor = m_taa->DeclareTaa(m_graph, litHdr, velocityT, depth, viewIndex, v->Width(), v->Height(),
                                                   m_taaBlend, m_taaGamma, m_taaMotionScale, /*near*/ 0.1f, taaFar);
                }
                // Transparent (blended) AFTER TAA, into the resolved image, with the UNJITTERED projection:
                // color-only, depth read-only against the opaque depth, back-to-front.
                m_pass.DeclareTransparent(*v, *m_registry, m_graph, m_frameIndex, viewIndex, sceneColor, depth,
                                          m_tonemap->HdrFormat(), unjitteredVP, prevViewProj, jitter, prevJitter, cluster, shadow, ibl);
                // Bloom pyramid over the resolved scene, composited by the tonemap.
                rendergraph::RGHandle bloomH{};
                if (m_bloom != nullptr && m_bloomIntensity > 0.0f) {
                    bloomH = m_bloom->DeclareBloom(m_graph, sceneColor, v->Width(), v->Height(), m_bloomThreshold, m_bloomKnee);
                }
                const f32 bloomStrength = bloomH.IsValid() ? m_bloomIntensity : 0.0f;
                const rendergraph::RGHandle bloomTex = bloomH.IsValid() ? bloomH : sceneColor;   // valid binding even when off
                // AO already applied pre-TAA; tonemap only needs the AO handle for the debug view.
                const f32 aoStrength = 0.0f;
                const rendergraph::RGHandle aoTex = aoH.IsValid() ? aoH : sceneColor;   // valid binding when off
                // Map the tonemap's fullscreen uv to this view's sub-rect of the (full-size) HDR/bloom,
                // so split-screen views resolve their own region (the forward renders into the sub-rect).
                const f32 fullW = static_cast<f32>(v->Width()), fullH = static_cast<f32>(v->Height());
                const Vec2 uvScale{ static_cast<f32>(v->ViewportWidth()) / fullW, static_cast<f32>(v->ViewportHeight()) / fullH };
                const Vec2 uvOffset{ static_cast<f32>(v->ViewportX()) / fullW, static_cast<f32>(v->ViewportY()) / fullH };
                // FXAA (TAA-off fallback) runs AFTER tonemap: tonemap -> LDR intermediate, FXAA -> final.
                // Never stacked with TAA (TAA already resolves aliasing). Off/TAA-on -> tonemap writes final.
                const bool fxaa = m_fxaa != nullptr && m_fxaaEnabled && !m_taaEnabled;
                const rendergraph::RGHandle tonemapOut = fxaa
                    ? m_graph.CreateTransient(u8"post.ldr", rendergraph::RGTextureDesc(v->TargetFormat(), v->Width(), v->Height()))
                    : colorH;
                m_tonemap->DeclareTonemap(m_graph, sceneColor, bloomTex, aoTex, tonemapOut, /*clearColor*/ fxaa || clearColor,
                                          v->Settings().clear, v->TargetFormat(),
                                          v->ViewportX(), v->ViewportY(), v->ViewportWidth(), v->ViewportHeight(),
                                          m_frameIndex, viewIndex, m_exposure, bloomStrength, uvScale, uvOffset, aoStrength, showAo);
                if (fxaa) {
                    const Vec2 texel{ 1.0f / fullW, 1.0f / fullH };
                    m_fxaa->DeclareFxaa(m_graph, tonemapOut, colorH, clearColor, v->Settings().clear, v->TargetFormat(),
                                        v->ViewportX(), v->ViewportY(), v->ViewportWidth(), v->ViewportHeight(),
                                        m_frameIndex, viewIndex, texel, uvScale, uvOffset, m_fxaaSubpixel);
                }
            } else {
                // No tonemap: forward writes the LDR target directly.
                m_pass.DeclarePass(*v, *m_registry, m_graph, m_frameIndex, viewIndex, colorH, depth, clearColor,
                                   v->TargetFormat(), normalT, velocityT, prevViewProj, jitter, prevJitter, cluster, shadow, ibl,
                                   rhi::LoadOp::Load, rendergraph::RGSubresourceRange{}, probePrefilteredH, probeActive);
                declareSky(colorH, velocityT, v->TargetFormat());
                m_pass.DeclareTransparent(*v, *m_registry, m_graph, m_frameIndex, viewIndex, colorH, depth,
                                          v->TargetFormat(), unjitteredVP, prevViewProj, jitter, prevJitter, cluster, shadow, ibl);
            }

            // Debug draw (per view): global + this view's scene gizmos, projected by the UNJITTERED VP,
            // into the final LDR (geometry depth-tested against the scene depth; screen text on top).
            // Keyed per-scene (+ global) so side-by-side scenes/views don't bleed.
            if (m_debugPass != nullptr) {
                const debug::DebugDraw* sceneDbg = static_cast<const debug::DebugDraw*>(v->DebugScene());
                if (m_debugGlobal != nullptr || sceneDbg != nullptr) {
                    m_debugPass->DeclareGeometry(m_graph, colorH, depth, unjitteredVP, m_debugGlobal, sceneDbg,
                                                 v->TargetFormat(), m_pass.DepthFormat(),
                                                 v->ViewportX(), v->ViewportY(), v->ViewportWidth(), v->ViewportHeight(), m_frameIndex, viewIndex);
                    m_debugPass->DeclareScreen(m_graph, colorH, unjitteredVP, m_debugGlobal, sceneDbg, v->TargetFormat(),
                                               v->ViewportX(), v->ViewportY(), v->ViewportWidth(), v->ViewportHeight(), m_frameIndex, viewIndex);
                }
            }
        }

        // View-independent screen HUD: drawn ONCE per distinct target at that target's full extent
        // (not per viewport), so a whole-window overlay isn't duplicated across split-screen views.
        // Declared after every view's passes so it composites on top. Screen-space only (identity VP;
        // 3D calls need a camera and are ignored here). Each target gets its own buffer slot.
        if (m_debugPass != nullptr && m_debugScreen != nullptr) {
            u32 overlayIndex = static_cast<u32>(m_views.ActiveCount());
            for (const TargetImport& ti : imported) {
                m_debugPass->DeclareScreen(m_graph, ti.handle, Mat4::Identity(), m_debugScreen, nullptr,
                                           ti.fmt, 0, 0, ti.w, ti.h, m_frameIndex, overlayIndex);
                ++overlayIndex;
            }
        }
        }   // end Compose.Declare
        if (m_decalPass != nullptr) { m_decalPass->EndFrame(); }   // unmap the decal ring before execute
        {
            DRACONIC_PROFILE_SCOPE("Compose.Execute");   // graph compile (barriers/transients) + record all passes
            (void)m_graph.Execute(m_encoder);
        }

        for (Renderer* r : m_registry->Unique()) { r->FinishFrame(); }
        m_prevViewProj = m_curViewProj;   // this frame's view-projs become next frame's "previous"
        m_prevJitter   = m_curJitter;     // ...and jitters (for the motion-vector unjitter)
        if (m_taaEnabled) { m_jitterIndex = (m_jitterIndex + 1u) % 8u; }   // Halton phase advances per frame
        m_encoder = nullptr;
    }

    [[nodiscard]] usize ViewCount() const noexcept { return m_views.ActiveCount(); }

private:
    RendererRegistry*       m_registry;
    ForwardPass             m_pass;
    rendergraph::RenderGraph m_graph;       // one graph per frame, composes all views
    ClusterSystem*          m_clusters = nullptr;   // borrowed; declares the per-view cluster build pass
    TonemapPass*            m_tonemap  = nullptr;   // borrowed; HDR-resolve pass (null => forward writes LDR direct)
    ShadowSystem*           m_shadows  = nullptr;   // borrowed; owns the directional shadow depth texture
    IBLSystem*              m_ibl      = nullptr;   // borrowed; owns the IBL precompute products (env/SH/prefilter/BRDF)
    SkyPass*                m_sky      = nullptr;   // borrowed; draws the visible environment background
    BloomPass*              m_bloom    = nullptr;   // borrowed; builds the HDR bloom pyramid (composited at tonemap)
    TaaPass*                m_taa      = nullptr;   // borrowed; temporal AA resolve (per-view history)
    AoPass*                 m_ao       = nullptr;   // borrowed; ambient occlusion (GTAO/SSAO) from the G-buffer
    FxaaPass*               m_fxaa     = nullptr;   // borrowed; TAA-off fallback AA (after tonemap)
    DecalPass*              m_decalPass = nullptr;  // borrowed; per-view screen-space decal pass
    ReflectionProbeSystem*  m_probeSystem = nullptr; // borrowed; dirty probes captured before the main views
    RenderView              m_captureViews[6];       // persistent 6-face capture views (outlive graph execute)
    DebugDrawPass*          m_debugPass = nullptr;  // borrowed; per-view debug gizmo/text pass
    const debug::DebugDraw* m_debugGlobal = nullptr;   // borrowed; global (all-views) debug list
    const debug::DebugDraw* m_debugScreen = nullptr;   // borrowed; whole-window screen HUD (drawn once)
    f32                     m_exposure = 1.0f;      // linear exposure multiplier (tonemap input)
    bool                    m_fxaaEnabled = false;
    f32                     m_fxaaSubpixel = 0.75f;
    AoMode                  m_aoMode      = AoMode::Off;
    i32                     m_aoDebug     = 0;
    f32                     m_aoStrength  = 0.6f;
    f32                     m_aoRadius    = 0.5f;
    f32                     m_aoIntensity = 1.0f;
    f32                     m_bloomIntensity = 0.05f;   // 0 = bloom off
    f32                     m_bloomThreshold = 1.0f;
    f32                     m_bloomKnee      = 0.6f;
    bool                    m_taaEnabled     = false;
    f32                     m_taaBlend       = 0.97f;
    f32                     m_taaGamma       = 1.25f;
    f32                     m_taaMotionScale = 32.0f;
    u32                     m_jitterIndex    = 0;    // Halton phase, advances once per frame (mod 8)
    // Motion vectors + TAA: last frame's view-proj + jitter per view index (this frame's collected into
    // m_curViewProj/m_curJitter, swapped in at End). Camera motion = prev vs current (jittered) view-proj.
    Array<Mat4>             m_prevViewProj;
    Array<Mat4>             m_curViewProj;
    Array<Vec2>             m_prevJitter;
    Array<Vec2>             m_curJitter;
    Array<ResolvedDraw>     m_prepassResolved;      // reused depth-draw buffer for the camera depth prepass
    Array<ResolvedDraw>     m_shadowResolved;       // reused depth-draw buffer for the shadow pass
    // Local-light (spot/point) shadows (5.3): per-frame caster matrices + their atlas tiles. Members
    // (not locals) so the atlas pass's execute lambda can reference the tiles for the frame's lifetime.
    struct LocalShadowTile { Mat4 viewProj; u32 x = 0, y = 0, w = 0, h = 0; Vec3 cullCenter; f32 cullRadius = 0.0f; };
    struct Sphere { Vec3 center; f32 radius = 0.0f; };   // a caster's world bounding sphere
    Array<GpuLocalShadow>   m_localShadows;        // flat buffer in caster order (shadowIndex indexes it)
    Array<LocalShadowTile>  m_rtTiles;             // realtime atlas layer tiles (re-rendered every frame)
    Array<LocalShadowTile>  m_staticTiles;         // static atlas layer tiles (cached; re-rendered per-tile on change)
    Array<LocalShadowTile>  m_staticRenderTiles;   // subset of m_staticTiles dirty THIS frame (rendered)
    Array<u32>              m_staticTileDirty;     // per-static-tile refresh countdown (index-stable across frames)
    Array<Sphere>           m_animatedSpheres;     // this frame's skinned-caster world spheres (per-tile routing)
    Array<DrawItem>         m_shadowCasters;       // camera-independent scene caster list (local shadows)
    Array<DrawItem>         m_shadowCullScratch;   // per-tile sphere-culled subset (reused)
    u64                     m_staticSig   = 0;     // signature of the static caster set (cache-invalidation)
    RenderViewPool          m_views;
    Array<DrawItem>         m_sortScratch;   // reused radix-sort ping-pong buffer
    rhi::CommandEncoder*    m_encoder    = nullptr;
    u32                     m_frameIndex = 0;
};

} // namespace draconic::render
