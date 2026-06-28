/// Raptor::Render — the `:pipeline` partition.
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
/// pass-group through raptor.rendergraph (MRT + automatic barriers + transient aliasing).

module;
#include "Core/Prelude.h"

export module raptor.render:pipeline;

import raptor.core;
import raptor.rhi;
import raptor.rendergraph;
import :data;
import :views;
import :cluster_system;

using namespace raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::render {

// What a Renderer needs to record draws for one view. `pass` is a RenderCommandEncoder — the
// shared draw-recording surface — so a renderer records identically whether it targets a live
// render pass or an off-thread render bundle (the basis for parallel command recording).
struct RenderRecordContext {
    const RenderView*          view        = nullptr;
    rhi::RenderCommandEncoder* pass        = nullptr;
    Mat4                       viewProj    = Mat4::Identity();
    Mat4                       viewMatrix  = Mat4::Identity();   // for view-space depth (clustered shading)
    Vec3                       cameraPos   = Vec3{ 0, 0, 0 };
    Vec3                       ambient     = Vec3{ 0.03f, 0.03f, 0.03f };   // scene environment ambient
    Span<const GpuLight>       lights      = {};
    ClusterBinding             cluster     = {};                 // per-cluster light lists (empty = clustering off)
    u32                        frameIndex  = 0;
    u32                        viewIndex   = 0;                  // this view's index in the frame (per-view buffer slots)
    rhi::TextureFormat         colorFormat = rhi::TextureFormat::BGRA8Unorm;
    rhi::TextureFormat         depthFormat = rhi::TextureFormat::Depth32Float;
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
    rhi::Buffer*         vertexBuffer1 = nullptr;  u64 vertexOffset1 = 0;   // optional instance stream
    rhi::Buffer*         indexBuffer  = nullptr;   u64 indexOffset = 0;
    rhi::IndexFormat     indexFormat  = rhi::IndexFormat::UInt32;
    u32                  indexCount   = 0;
    u32                  instanceCount = 1;
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

    virtual void FinishFrame() {}
};

// Routes a RenderCategory to its registered Renderer. A small dense table keyed by category
// id (so external categories beyond the built-ins just index higher slots), plus the list of
// distinct renderers for frame-bracket iteration.
class RendererRegistry {
public:
    // Register `renderer` (borrowed; the caller owns its lifetime) for all its categories.
    void Register(Renderer* renderer) {
        if (renderer == nullptr) { return; }
        m_unique.PushBack(renderer);
        for (RenderCategory c : renderer->SupportedCategories()) {
            if (c < kMaxCategories) { m_byCategory[c] = renderer; }
        }
    }

    [[nodiscard]] Renderer* ForCategory(RenderCategory c) const noexcept {
        return (c < kMaxCategories) ? m_byCategory[c] : nullptr;
    }

    [[nodiscard]] Span<Renderer* const> Unique() const noexcept {
        return Span<Renderer* const>{ m_unique.Data(), m_unique.Size() };
    }

private:
    Array<Renderer*> m_unique;
    Renderer*        m_byCategory[kMaxCategories] = {};
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
    void DeclarePass(const RenderView& view, const RendererRegistry& registry,
                     rendergraph::RenderGraph& graph, u32 frameIndex, u32 viewIndex, const ClusterBinding& cluster = {}) {
        rhi::TextureView* color = view.Target();
        if (color == nullptr || view.Width() == 0 || view.Height() == 0) { return; }

        const rendergraph::RGHandle depth = graph.CreateTransient(
            u8"forward.depth", rendergraph::RGTextureDesc(m_depthFormat, view.Width(), view.Height()));
        // current==final==RenderTarget: the host did Undefined->RenderTarget and will do
        // RenderTarget->Present, so the graph touches no backbuffer barrier.
        const rendergraph::RGHandle colorH = graph.ImportTarget(
            u8"forward.color", nullptr, color, rhi::ResourceState::RenderTarget, rhi::ResourceState::RenderTarget);

        graph.AddRenderPass(u8"forward", [this, &view, &registry, depth, colorH, frameIndex, viewIndex, cluster](rendergraph::PassBuilder& b) {
            b.SetColorTarget(0, colorH, rhi::LoadOp::Clear, rhi::StoreOp::Store, view.Settings().clear);
            b.SetDepthTarget(depth, rhi::LoadOp::Clear, rhi::StoreOp::Store);
            // Read the cluster lists the build compute pass wrote (orders compute -> this pass).
            if (cluster.Valid()) { b.ReadBuffer(cluster.offsetsHandle); b.ReadBuffer(cluster.indicesHandle); }
            b.NeverCull();
            b.SetBundleExecute([this, &view, &registry, frameIndex, viewIndex, cluster](rhi::CommandEncoder& enc, Array<rhi::RenderBundle*>& out) {
                ResolveAndEmit(view, registry, enc, frameIndex, viewIndex, cluster, out);
            });
        });
    }

private:
    // The bundle-pass body: resolve the view's draws (single-threaded) then emit them into render
    // bundle(s) appended to `out` — serially below the threshold, else fanned out across the job
    // system (per-worker bundles). The graph replays `out` via ExecuteBundles.
    void ResolveAndEmit(const RenderView& view, const RendererRegistry& registry,
                        rhi::CommandEncoder& encoder, u32 frameIndex, u32 viewIndex, const ClusterBinding& cluster,
                        Array<rhi::RenderBundle*>& out) {
        RenderRecordContext ctx{};
        ctx.view        = &view;
        ctx.viewProj    = view.Camera().ViewProjection();
        ctx.viewMatrix  = view.Camera().view;
        ctx.cameraPos   = view.Camera().position;
        ctx.ambient     = (view.Scene() != nullptr) ? view.Scene()->Ambient() : Vec3{ 0.03f, 0.03f, 0.03f };
        ctx.lights      = (view.Scene() != nullptr) ? view.Scene()->Lights() : Span<const GpuLight>{};
        ctx.cluster     = cluster;
        ctx.frameIndex  = frameIndex;
        ctx.viewIndex   = viewIndex;
        ctx.colorFormat = view.TargetFormat();
        ctx.depthFormat = m_depthFormat;

        // RESOLVE (single-threaded): sorted draw list -> ResolvedDraws (PSO build, mesh upload,
        // ring allocation). Equal-category items are contiguous; each run goes to its renderer.
        m_resolved.Clear();
        const Span<const DrawItem> items = view.DrawList();
        usize i = 0;
        while (i < items.Size()) {
            const RenderCategory cat = items[i].data->category;
            usize j = i + 1;
            while (j < items.Size() && items[j].data->category == cat) { ++j; }
            if (Renderer* r = registry.ForCategory(cat)) {
                r->Resolve(ctx, Span<const DrawItem>{ items.Data() + i, j - i }, m_resolved);
            }
            i = j;
        }

        // EMIT into bundles.
        rhi::RenderBundleDesc bd{};
        bd.colorFormats[0]    = view.TargetFormat();
        bd.colorFormatCount   = 1;
        bd.depthStencilFormat = m_depthFormat;
        bd.sampleCount        = 1;
        bd.width              = view.Width();
        bd.height             = view.Height();
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

// The single per-frame driver. Begin resets shared per-frame state; AddView collects a view
// (extracting its draw list from a scene snapshot); End sizes the renderers' transient once
// for the whole frame and composes every view. One driver, all views — no per-view object.
class RenderFrame {
public:
    RenderFrame(rhi::Device& device, RendererRegistry& registry, u32 framesInFlight,
                ClusterSystem* clusters = nullptr) noexcept
        : m_registry(&registry), m_pass(device, framesInFlight), m_graph(&device), m_clusters(clusters) {}

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
                        rhi::TextureView* target, rhi::TextureFormat targetFormat, u32 width, u32 height) {
        RenderView* view = m_views.Acquire();
        view->Bind(scene, camera, settings, target, targetFormat, width, height);
        view->BuildDrawList(m_sortScratch);
        return view;
    }

    // Compose all collected views into the frame's encoder.
    void End() {
        if (m_encoder == nullptr) { return; }

        u32 totalDraws = 0;
        for (usize i = 0; i < m_views.ActiveCount(); ++i) {
            totalDraws += static_cast<u32>(m_views.At(i)->DrawList().Size());
        }

        for (Renderer* r : m_registry->Unique()) { r->PrepareFrame(totalDraws, m_frameIndex); }
        if (m_clusters != nullptr) { m_clusters->PrepareFrame(m_frameIndex); }   // size the cluster build's per-frame buffers
        m_pass.BeginFrame(m_frameIndex);   // reset per-worker pools once (before any view)

        // Declare every view's forward pass into the one frame graph, then let the graph compile
        // (barriers + transient depth allocation/aliasing) + execute. (§9: one graph, all views.)
        if (m_views.ActiveCount() > 0) {
            m_graph.SetOutputSize(m_views.At(0)->Width(), m_views.At(0)->Height());
        }
        for (usize i = 0; i < m_views.ActiveCount(); ++i) {
            // Cluster build (compute) declared before the view's forward pass so the graph orders
            // the light-binning write ahead of the shading read; its binding feeds the forward pass.
            // viewIndex isolates per-view cluster buffers (two views/frame must not share a slot).
            const u32 viewIndex = static_cast<u32>(i);
            ClusterBinding cluster;
            if (m_clusters != nullptr) { cluster = m_clusters->DeclareBuild(m_graph, *m_views.At(i), m_frameIndex, viewIndex); }
            m_pass.DeclarePass(*m_views.At(i), *m_registry, m_graph, m_frameIndex, viewIndex, cluster);
        }
        (void)m_graph.Execute(m_encoder);

        for (Renderer* r : m_registry->Unique()) { r->FinishFrame(); }
        m_encoder = nullptr;
    }

    [[nodiscard]] usize ViewCount() const noexcept { return m_views.ActiveCount(); }

private:
    RendererRegistry*       m_registry;
    ForwardPass             m_pass;
    rendergraph::RenderGraph m_graph;       // one graph per frame, composes all views
    ClusterSystem*          m_clusters = nullptr;   // borrowed; declares the per-view cluster build pass
    RenderViewPool          m_views;
    Array<DrawItem>         m_sortScratch;   // reused radix-sort ping-pong buffer
    rhi::CommandEncoder*    m_encoder    = nullptr;
    u32                     m_frameIndex = 0;
};

} // namespace raptor::render
