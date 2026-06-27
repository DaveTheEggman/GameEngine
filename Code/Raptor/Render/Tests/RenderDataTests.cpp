// The scene-agnostic render-data core (no GPU): the frame arena, ExtractedScene snapshot,
// sort keys + radix sort, the RenderView draw-list build (cull/sort), and the view pool.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import raptor.core;
import raptor.rhi;
import raptor.render;

using namespace raptor::core;
using namespace raptor::render;
namespace rhi = raptor::rhi;

TEST_CASE("FrameArena: allocations are distinct, aligned, and reset reuses chunks")
{
    FrameArena arena;
    MeshRenderData* a = arena.New<MeshRenderData>();
    MeshRenderData* b = arena.New<MeshRenderData>();
    MeshRenderData* c = arena.New<MeshRenderData>();
    REQUIRE(a != nullptr); REQUIRE(b != nullptr); REQUIRE(c != nullptr);
    CHECK(a != b); CHECK(b != c);
    CHECK(reinterpret_cast<usize>(a) % alignof(MeshRenderData) == 0);
    CHECK(reinterpret_cast<usize>(b) % alignof(MeshRenderData) == 0);

    const usize chunksAfterFirst = arena.ChunkCount();
    arena.Reset();
    for (int i = 0; i < 3; ++i) { CHECK(arena.New<MeshRenderData>() != nullptr); }
    CHECK(arena.ChunkCount() == chunksAfterFirst);   // same load reuses chunks, no growth
}

TEST_CASE("ExtractedScene: Add registers items; Reset empties without freeing chunks")
{
    ExtractedScene scene;
    CHECK(scene.IsEmpty());
    for (int i = 0; i < 10; ++i) {
        MeshRenderData* rd = scene.Add<MeshRenderData>();
        REQUIRE(rd != nullptr);
        rd->entityId = static_cast<u64>(i);
    }
    REQUIRE(scene.Size() == 10);
    CHECK(scene.Items()[5]->category == RenderCategories::Opaque);

    scene.Reset();
    CHECK(scene.IsEmpty());
    CHECK(scene.Size() == 0);
}

TEST_CASE("sort keys: category dominates, then state, then depth")
{
    // Category is most significant: any opaque key < any transparent key.
    const u64 opaque      = MakeSortKey(RenderCategories::Opaque, 0xFFFFFF, 0xFFFFFF);
    const u64 transparent = MakeSortKey(RenderCategories::Transparent, 0, 0);
    CHECK(opaque < transparent);

    // Within a category, smaller depth sorts first (front-to-back for opaque).
    const u64 near = MakeSortKey(RenderCategories::Opaque, 7, QuantizeDepth(0.10f, /*invert*/ false));
    const u64 far  = MakeSortKey(RenderCategories::Opaque, 7, QuantizeDepth(0.90f, /*invert*/ false));
    CHECK(near < far);

    // Inverted depth (transparent) reverses it (back-to-front).
    const u64 tNear = MakeSortKey(RenderCategories::Transparent, 0, QuantizeDepth(0.10f, /*invert*/ true));
    const u64 tFar  = MakeSortKey(RenderCategories::Transparent, 0, QuantizeDepth(0.90f, /*invert*/ true));
    CHECK(tFar < tNear);
}

TEST_CASE("RadixSortDrawItems sorts ascending by key")
{
    Array<DrawItem> items;
    const u64 keys[] = { 50, 3, 9999, 0, 42, 7, 0x00FF00FF00FF00FFull, 1, 256, 255 };
    for (u64 k : keys) { items.PushBack(DrawItem{ k, nullptr }); }

    Array<DrawItem> scratch;
    RadixSortDrawItems(items, scratch);

    REQUIRE(items.Size() == 10);
    for (usize i = 1; i < items.Size(); ++i) { CHECK(items[i - 1].key <= items[i].key); }
    CHECK(items[0].key == 0);
    CHECK(items[items.Size() - 1].key == 0x00FF00FF00FF00FFull);
}

TEST_CASE("RenderView::BuildDrawList sorts opaque front-to-back, transparent back-to-front")
{
    // Three meshes in front of an identity camera at view-space depths 2, 5, 8.
    ExtractedScene scene;
    auto add = [&](f32 z, u64 tag, RenderCategory cat) {
        MeshRenderData* rd = scene.Add<MeshRenderData>();
        rd->worldCenter = Vec3{ 0, 0, z };   // camera looks down -z, so z<0 is in front
        rd->entityId    = tag;
        rd->category    = cat;
    };

    ViewCamera camera;                       // identity view, farZ 1000
    ViewSettings settings;
    Array<DrawItem> scratch;

    SUBCASE("opaque: nearest first")
    {
        add(-2.0f, /*tag*/ 2, RenderCategories::Opaque);
        add(-8.0f, /*tag*/ 8, RenderCategories::Opaque);
        add(-5.0f, /*tag*/ 5, RenderCategories::Opaque);

        RenderView view;
        view.Bind(scene, camera, settings, nullptr, rhi::TextureFormat::BGRA8Unorm, 64, 64);
        view.BuildDrawList(scratch);

        const Span<const DrawItem> dl = view.DrawList();
        REQUIRE(dl.Size() == 3);
        CHECK(static_cast<const MeshRenderData*>(dl[0].data)->entityId == 2);  // nearest
        CHECK(static_cast<const MeshRenderData*>(dl[1].data)->entityId == 5);
        CHECK(static_cast<const MeshRenderData*>(dl[2].data)->entityId == 8);  // farthest
    }

    SUBCASE("transparent: farthest first")
    {
        add(-2.0f, 2, RenderCategories::Transparent);
        add(-8.0f, 8, RenderCategories::Transparent);
        add(-5.0f, 5, RenderCategories::Transparent);

        RenderView view;
        view.Bind(scene, camera, settings, nullptr, rhi::TextureFormat::BGRA8Unorm, 64, 64);
        view.BuildDrawList(scratch);

        const Span<const DrawItem> dl = view.DrawList();
        REQUIRE(dl.Size() == 3);
        CHECK(static_cast<const MeshRenderData*>(dl[0].data)->entityId == 8);  // farthest
        CHECK(static_cast<const MeshRenderData*>(dl[2].data)->entityId == 2);  // nearest
    }
}

TEST_CASE("RenderViewPool: Acquire hands out stable views; Begin rewinds")
{
    RenderViewPool pool;
    pool.Begin();
    RenderView* a = pool.Acquire();
    RenderView* b = pool.Acquire();
    REQUIRE(a != nullptr); REQUIRE(b != nullptr);
    CHECK(a != b);
    CHECK(pool.ActiveCount() == 2);

    pool.Begin();
    CHECK(pool.ActiveCount() == 0);
    RenderView* a2 = pool.Acquire();
    CHECK(a2 == a);                          // pooled storage reused, addresses stable
}

TEST_CASE("RendererRegistry routes categories to renderers")
{
    struct FakeRenderer final : Renderer {
        Span<const RenderCategory> SupportedCategories() const override {
            static constexpr RenderCategory cats[] = { RenderCategories::Opaque, RenderCategories::Masked };
            return Span<const RenderCategory>{ cats, 2 };
        }
        void Record(const RenderRecordContext&, Span<const DrawItem>) override {}
    };

    FakeRenderer r;
    RendererRegistry registry;
    registry.Register(&r);

    CHECK(registry.ForCategory(RenderCategories::Opaque) == &r);
    CHECK(registry.ForCategory(RenderCategories::Masked) == &r);
    CHECK(registry.ForCategory(RenderCategories::Transparent) == nullptr);   // unregistered
    CHECK(registry.Unique().Size() == 1);
}
