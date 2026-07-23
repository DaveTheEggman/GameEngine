// Draconic GUI - Thickness + LayerDrawable (background compositor) tests, including the
// Phase 2 vertical slice: a styled panel (background + border) composited through VG.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.vg;
import draconic.gui;

using namespace draconic::gui;
namespace core = draconic::core;
namespace vg = draconic::vg;

// === Thickness ===

TEST_CASE("thickness: defaults and constructors")
{
    Thickness zero;
    CHECK(zero.IsZero());

    Thickness all{4.0f};
    CHECK(all.Left == 4.0f);
    CHECK(all.Bottom == 4.0f);
    CHECK(all.TotalHorizontal() == doctest::Approx(8.0f));
    CHECK(all.TotalVertical() == doctest::Approx(8.0f));

    Thickness hv{2.0f, 6.0f};
    CHECK(hv.Left == 2.0f);
    CHECK(hv.Right == 2.0f);
    CHECK(hv.Top == 6.0f);
    CHECK(hv.Bottom == 6.0f);

    Thickness each{1.0f, 2.0f, 3.0f, 4.0f};
    CHECK(each.Left == 1.0f);
    CHECK(each.Top == 2.0f);
    CHECK(each.Right == 3.0f);
    CHECK(each.Bottom == 4.0f);
    CHECK_FALSE(each.IsZero());
    CHECK(each == Thickness{1.0f, 2.0f, 3.0f, 4.0f});
}

// === LayerDrawable ===

TEST_CASE("compositor: AddLayer increments count")
{
    LayerDrawable layers;
    CHECK(layers.LayerCount() == 0);
    layers.AddLayer(core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), core::Color::Red));
    layers.AddLayer(core::MakeRef<BorderDrawable>(core::DefaultAllocator()), Thickness{2.0f});
    CHECK(layers.LayerCount() == 2);
    layers.ClearLayers();
    CHECK(layers.LayerCount() == 0);
}

TEST_CASE("compositor: empty draws nothing")
{
    vg::VGContext ctx;
    DrawContext dc{ctx};
    LayerDrawable layers;
    layers.Draw(dc, Rect{0.0f, 0.0f, 50.0f, 50.0f});
    CHECK(ctx.GetBatch().vertices.Size() == 0);
}

// The Phase 2 vertical slice: a styled panel = background fill + inset border, one
// composited drawable, rendered through VG.
TEST_CASE("compositor: panel background + border produces geometry")
{
    auto background = core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), core::Color::Blue);
    background->SetCornerRadii(vg::CornerRadii{6.0f});

    auto border = core::MakeRef<BorderDrawable>(core::DefaultAllocator(),
                                                core::Color{0.0f, 0.0f, 0.0f, 1.0f}, 2.0f);
    border->SetCornerRadii(vg::CornerRadii{6.0f});

    LayerDrawable panel;
    panel.AddLayer(background);
    panel.AddLayer(border);

    vg::VGContext ctx;
    DrawContext dc{ctx};
    panel.Draw(dc, Rect{10.0f, 10.0f, 200.0f, 120.0f});
    CHECK(ctx.GetBatch().vertices.Size() > 0);
}

TEST_CASE("compositor: state-aware draw dispatches to layers")
{
    LayerDrawable layers;
    StateListDrawable* stateful = nullptr;
    {
        auto sl = core::MakeRef<StateListDrawable>(core::DefaultAllocator());
        sl->Set(ControlState::Normal,
                core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), core::Color::Red));
        stateful = sl.Get();
        layers.AddLayer(sl);
    }
    CHECK(stateful != nullptr);

    vg::VGContext ctx;
    DrawContext dc{ctx};
    layers.Draw(dc, Rect{0.0f, 0.0f, 30.0f, 30.0f}, ControlState::Hover); // -> Normal fallback
    CHECK(ctx.GetBatch().vertices.Size() > 0);
}
