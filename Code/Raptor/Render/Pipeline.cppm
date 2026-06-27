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
import :data;
import :views;

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
    rhi::TextureFormat         colorFormat = rhi::TextureFormat::BGRA8Unorm;
    rhi::TextureFormat         depthFormat = rhi::TextureFormat::Depth32Float;
};

// A per-category drawer. Implemented by mesh/sprite/particle/etc. subsystems and registered
// with the RendererRegistry. `Record` is called with a contiguous, pre-sorted run of this
// renderer's DrawItems (all of one category) and an open render pass. PrepareFrame/FinishFrame
// bracket the whole frame (all views) so a renderer can size + map its transient buffers once.
class Renderer {
public:
    virtual ~Renderer() = default;

    // The categories this renderer draws (its registration keys).
    [[nodiscard]] virtual Span<const RenderCategory> SupportedCategories() const = 0;

    // Bracket the frame: `maxDraws` is an upper bound on DrawItems this renderer may receive
    // across all views, so per-object transient (e.g. the object-UBO ring) is sized once and
    // never reallocated mid-frame (which would invalidate already-recorded draws). `frameIndex`
    // is the device ring slot, selecting this frame's region of any frames-in-flight ring.
    virtual void PrepareFrame(u32 maxDraws, u32 frameIndex) { (void)maxDraws; (void)frameIndex; }

    // Record `items` (a sorted run of this renderer's categories) into `ctx.pass`.
    virtual void Record(const RenderRecordContext& ctx, Span<const DrawItem> items) = 0;

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
    explicit ForwardPass(rhi::Device& device) noexcept : m_device(&device) {}
    ~ForwardPass() { ReleaseDepth(); }

    ForwardPass(const ForwardPass&) = delete;
    ForwardPass& operator=(const ForwardPass&) = delete;

    void Execute(const RenderView& view, const RendererRegistry& registry, rhi::CommandEncoder& encoder) {
        rhi::TextureView* color = view.Target();
        if (color == nullptr || !EnsureDepth(view.Width(), view.Height())) { return; }

        rhi::RenderPassDesc rp{};
        rhi::ColorAttachment ca{};
        ca.view = color; ca.loadOp = rhi::LoadOp::Clear; ca.storeOp = rhi::StoreOp::Store;
        ca.clearValue = view.Settings().clear;
        rp.colorAttachments.Add(ca);
        rhi::DepthStencilAttachment ds{};
        ds.view = m_depthView; ds.depthLoadOp = rhi::LoadOp::Clear; ds.depthStoreOp = rhi::StoreOp::Store;
        ds.depthClearValue = 1.0f;
        rp.depthStencilAttachment = ds;
        rp.label = u8"forward";

        RenderRecordContext ctx{};
        ctx.view        = &view;
        ctx.viewProj    = view.Camera().ViewProjection();
        ctx.colorFormat = view.TargetFormat();
        ctx.depthFormat = m_depthFormat;

        // Record the view's draws into a render bundle FIRST — bundle recording is independent
        // of the pass and must happen before BeginRenderPass (the encoder must be in the
        // recording state). This is the seam for parallel recording: a future split records N
        // bundles on N threads here. The bundle is then replayed into the pass below.
        rhi::RenderBundleDesc bd{};
        bd.colorFormats[0]     = view.TargetFormat();
        bd.colorFormatCount    = 1;
        bd.depthStencilFormat  = m_depthFormat;
        bd.sampleCount         = 1;
        bd.width               = view.Width();
        bd.height              = view.Height();
        bd.label               = u8"forward.bundle";

        const Span<const DrawItem> items = view.DrawList();
        rhi::RenderBundle* bundle = nullptr;
        if (rhi::RenderBundleEncoder* be = encoder.CreateRenderBundleEncoder(bd)) {
            ctx.pass = be;
            RecordRange(items, 0, items.Size(), registry, ctx);
            bundle = be->Finish();
        }

        // Depth target starts Undefined each frame (we clear it); move it to depth-write
        // before the pass. (The color target was transitioned to RenderTarget by the host.)
        encoder.TransitionTexture(m_depthTex, rhi::ResourceState::Undefined, rhi::ResourceState::DepthStencilWrite);

        // The pass body is supplied by the bundle (no inline draws — secondary contents).
        rp.contents = rhi::RenderPassContents::SecondaryCommandBuffers;
        rhi::RenderPassEncoder* pass = encoder.BeginRenderPass(rp);
        if (pass == nullptr) { return; }
        if (bundle != nullptr) {
            rhi::RenderBundle* bundles[1] = { bundle };
            pass->ExecuteBundles(Span<rhi::RenderBundle* const>{ bundles, 1 });
        }
        pass->End();
    }

private:
    // Dispatch the sorted draw-list range [begin, end) to the registered Renderers. The list is
    // sorted with category in the key's MSBs, so equal-category items are contiguous; each run
    // goes to that category's renderer. (A parallel split calls this per chunk into its bundle.)
    static void RecordRange(Span<const DrawItem> items, usize begin, usize end,
                            const RendererRegistry& registry, const RenderRecordContext& ctx) {
        usize i = begin;
        while (i < end) {
            const RenderCategory cat = items[i].data->category;
            usize j = i + 1;
            while (j < end && items[j].data->category == cat) { ++j; }
            if (Renderer* r = registry.ForCategory(cat)) {
                r->Record(ctx, Span<const DrawItem>{ items.Data() + i, j - i });
            }
            i = j;
        }
    }

    bool EnsureDepth(u32 width, u32 height) {
        if (width == 0 || height == 0) { return false; }
        if (m_depthTex != nullptr && m_depthW == width && m_depthH == height) { return true; }
        ReleaseDepth();
        rhi::TextureDesc td = rhi::TextureDesc::DepthBuffer(m_depthFormat, width, height, 1, u8"forward.depth");
        if (!m_device->CreateTexture(td, m_depthTex).IsOk()) { m_depthTex = nullptr; return false; }
        rhi::TextureViewDesc vd{};
        vd.format = m_depthFormat; vd.dimension = rhi::TextureViewDimension::Texture2D;
        vd.aspect = rhi::TextureAspect::DepthOnly;
        if (!m_device->CreateTextureView(m_depthTex, vd, m_depthView).IsOk()) { m_depthView = nullptr; return false; }
        m_depthW = width; m_depthH = height;
        return true;
    }

    void ReleaseDepth() {
        if (m_depthView) { m_device->DestroyTextureView(m_depthView); m_depthView = nullptr; }
        if (m_depthTex)  { m_device->DestroyTexture(m_depthTex); m_depthTex = nullptr; }
        m_depthW = 0; m_depthH = 0;
    }

    rhi::Device*       m_device;
    rhi::Texture*      m_depthTex   = nullptr;
    rhi::TextureView*  m_depthView  = nullptr;
    u32                m_depthW = 0, m_depthH = 0;
    rhi::TextureFormat m_depthFormat = rhi::TextureFormat::Depth32Float;
};

// The single per-frame driver. Begin resets shared per-frame state; AddView collects a view
// (extracting its draw list from a scene snapshot); End sizes the renderers' transient once
// for the whole frame and composes every view. One driver, all views — no per-view object.
class RenderFrame {
public:
    RenderFrame(rhi::Device& device, RendererRegistry& registry) noexcept
        : m_registry(&registry), m_pass(device) {}

    // Begin a frame against the caller's encoder (the caller owns the encoder + targets).
    void Begin(rhi::CommandEncoder& encoder, u32 frameIndex) noexcept {
        m_encoder    = &encoder;
        m_frameIndex = frameIndex;
        m_views.Begin();
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
        for (usize i = 0; i < m_views.ActiveCount(); ++i) {
            m_pass.Execute(*m_views.At(i), *m_registry, *m_encoder);
        }
        for (Renderer* r : m_registry->Unique()) { r->FinishFrame(); }

        m_encoder = nullptr;
    }

    [[nodiscard]] usize ViewCount() const noexcept { return m_views.ActiveCount(); }

private:
    RendererRegistry*    m_registry;
    ForwardPass          m_pass;
    RenderViewPool       m_views;
    Array<DrawItem>      m_sortScratch;   // reused radix-sort ping-pong buffer
    rhi::CommandEncoder* m_encoder    = nullptr;
    u32                  m_frameIndex = 0;
};

} // namespace raptor::render
