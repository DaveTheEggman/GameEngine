/// Raptor::Render — the `:views` partition.
///
/// `RenderView` is the renderer's unit of work *and* its isolation boundary: render one
/// scene's extracted data, from one camera, into one target. A frame renders a SET of
/// views — primary cameras, plus (later) derived shadow/probe views — and `RenderView`
/// owning all per-frame-mutable state (the culled+sorted draw list, and later the view
/// UBO, cluster grid, shadow slots, HDR/depth targets) is what guarantees views don't
/// trash each other. The only state shared across views is the immutable `ExtractedScene`
/// and shared GPU resources. (§9 of docs/design/renderer.md.)
///
/// Views are pooled per frame from a `RenderViewPool` (reset, not freed, each frame).

module;
#include "Core/Prelude.h"

export module raptor.render:views;

import raptor.core;
import raptor.rhi;
import :data;

using namespace raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::render {

// The camera for a view: world→view and view→clip, plus the world-space eye (for depth
// sorting / culling). The view matrix is the inverse of the camera entity's world matrix.
struct ViewCamera {
    Mat4 view       = Mat4::Identity();
    Mat4 projection = Mat4::Identity();
    Vec3 position   = Vec3{ 0, 0, 0 };
    f32  farZ       = 1000.0f;   // for depth-key normalization

    [[nodiscard]] Mat4 ViewProjection() const noexcept { return view * projection; }
};

// Per-view settings (grows with post config, layer mask, etc. in later phases).
struct ViewSettings {
    rhi::ClearColor clear = rhi::ClearColor::CornflowerBlue();
};

// A single view: what to draw (a shared ExtractedScene), from where (camera), into what
// (target). Owns the per-view draw list. Pooled — `Reset` rebinds it for a new use without
// freeing the draw-list storage.
class RenderView {
public:
    // Bind this view to a scene snapshot + camera + target for the current frame.
    void Bind(const ExtractedScene& scene, const ViewCamera& camera, const ViewSettings& settings,
              rhi::TextureView* target, rhi::TextureFormat targetFormat, u32 width, u32 height) noexcept {
        m_scene        = &scene;
        m_camera       = camera;
        m_settings     = settings;
        m_target       = target;
        m_targetFormat = targetFormat;
        m_width        = width;
        m_height       = height;
        m_drawList.Clear();
    }

    // Build the per-view draw list from the bound scene: assign each renderable a category +
    // a view-dependent sort key (state bits + view-space depth), then radix-sort. Phase 1
    // does no frustum culling (everything is drawn); the cull step slots in here later.
    void BuildDrawList(Array<DrawItem>& sortScratch) {
        m_drawList.Clear();
        if (m_scene == nullptr) { return; }

        const Mat4 viewMat = m_camera.view;
        const f32  invFar  = (m_camera.farZ > 0.0f) ? (1.0f / m_camera.farZ) : 1.0f;

        for (RenderData* data : m_scene->Items()) {
            if (data == nullptr) { continue; }
            // Phase 1 only knows mesh data; other categories route through their own
            // producers + renderers and would compute their own keys.
            const auto* mesh = static_cast<const MeshRenderData*>(data);

            // view-space depth: forward is -z in RH view space, so distance ~ -z_view.
            const Vec3 vc      = TransformPoint(mesh->worldCenter, viewMat);
            const f32  depth01 = (-vc.z) * invFar;

            const bool transparent = (data->category == RenderCategories::Transparent);
            // Opaque/masked: cluster by (mesh, material) so identical draws are contiguous and
            // batchable into one instanced draw (depth sub-orders within a batch). Transparent:
            // zero the state bits so depth dominates — back-to-front order must not be broken.
            const u32  stateBits   = transparent ? 0u : BatchBits(mesh->mesh, mesh->material);
            const u32  depthBits   = QuantizeDepth(depth01, /*invert*/ transparent);
            const u64  key         = MakeSortKey(data->category, stateBits, depthBits);

            m_drawList.PushBack(DrawItem{ key, data });
        }

        RadixSortDrawItems(m_drawList, sortScratch);
    }

    [[nodiscard]] const ExtractedScene*     Scene()       const noexcept { return m_scene; }
    [[nodiscard]] const ViewCamera&         Camera()      const noexcept { return m_camera; }
    [[nodiscard]] const ViewSettings&       Settings()    const noexcept { return m_settings; }
    [[nodiscard]] rhi::TextureView*         Target()      const noexcept { return m_target; }
    [[nodiscard]] rhi::TextureFormat        TargetFormat()const noexcept { return m_targetFormat; }
    [[nodiscard]] u32                       Width()       const noexcept { return m_width; }
    [[nodiscard]] u32                       Height()      const noexcept { return m_height; }
    [[nodiscard]] Span<const DrawItem>      DrawList()    const noexcept {
        return Span<const DrawItem>{ m_drawList.Data(), m_drawList.Size() };
    }

private:
    // Cluster batchable draws (same mesh + material -> same PSO + vertex buffer) together in
    // the sort key, so they end up contiguous and the renderer can fuse them into one instanced
    // draw. Identity is the (mesh, material) pair folded to the 24-bit state field. Pointer-
    // derived is fine for a *transient* per-frame sort key (the design's no-pointer-identity
    // rule is about the persistent batch CACHE, not this); the renderer re-checks exact pointer
    // equality when forming a batch, so a hash collision only costs a missed fusion, never a
    // wrong draw.
    [[nodiscard]] static u32 BatchBits(const void* mesh, const void* material) noexcept {
        const usize m = reinterpret_cast<usize>(mesh);
        const usize n = reinterpret_cast<usize>(material);
        const usize mixed = (m >> 4) * 1099511628211ull + (n >> 4);
        return static_cast<u32>(mixed & ((1u << kSortStateBits) - 1));
    }

    const ExtractedScene* m_scene        = nullptr;
    ViewCamera            m_camera;
    ViewSettings          m_settings;
    rhi::TextureView*     m_target        = nullptr;
    rhi::TextureFormat    m_targetFormat  = rhi::TextureFormat::BGRA8Unorm;
    u32                   m_width         = 0;
    u32                   m_height        = 0;
    Array<DrawItem>       m_drawList;   // per-view, owned (pooled storage)
};

// A per-frame pool of views. `Begin` rewinds it (keeping the RenderView storage + their
// draw-list buffers); `Acquire` hands out the next view. Views stay valid until the next
// Begin. Stable addresses: views live in UniquePtr slots so growing the pool never moves
// an already-acquired view.
class RenderViewPool {
public:
    void Begin() noexcept { m_count = 0; }

    [[nodiscard]] RenderView* Acquire() {
        if (m_count == m_views.Size()) { m_views.PushBack(MakeUnique<RenderView>(DefaultAllocator())); }
        return m_views[m_count++].Get();
    }

    [[nodiscard]] usize ActiveCount() const noexcept { return m_count; }
    [[nodiscard]] RenderView* At(usize i) const noexcept { return m_views[i].Get(); }

private:
    Array<UniquePtr<RenderView>> m_views;
    usize                        m_count = 0;
};

} // namespace raptor::render
