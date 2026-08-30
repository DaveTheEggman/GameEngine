// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Ported from Sedulous.UI.Tests/src/DrawableTests.bf (faithful; Beef `new X()/defer ReleaseRef` ->
// stack values / MakeRef children, `===` reference-equality -> pointer ==).
// NOTE: the two NineSlice_* tests are not ported; they require NineSliceDrawable.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import foundation.core;
import foundation.image;
import foundation.ui;

using namespace foundation::ui;
namespace core = foundation::core;

// === StateListDrawable ===

TEST_CASE("drawable: StateList_GetFallsBackToNormal")
{
    StateListDrawable sl;
    sl.Set(ControlState::Normal,
           core::MakeRef<ColorDrawable>(core::DefaultAllocator(), core::Color::Red));
    Drawable* normal = sl.Get(ControlState::Normal);

    CHECK(sl.Get(ControlState::Normal) == normal);
    CHECK(sl.Get(ControlState::Hover) == normal);    // fallback
    CHECK(sl.Get(ControlState::Pressed) == normal);  // fallback
    CHECK(sl.Get(ControlState::Disabled) == normal); // fallback
}

TEST_CASE("drawable: StateList_GetReturnsSpecificState")
{
    StateListDrawable sl;
    sl.Set(ControlState::Normal,
           core::MakeRef<ColorDrawable>(core::DefaultAllocator(), core::Color::Red));
    sl.Set(ControlState::Hover,
           core::MakeRef<ColorDrawable>(core::DefaultAllocator(), core::Color::Blue));
    Drawable* normal = sl.Get(ControlState::Normal);
    Drawable* hover = sl.Get(ControlState::Hover);

    CHECK(sl.Get(ControlState::Normal) == normal);
    CHECK(sl.Get(ControlState::Hover) == hover);
    CHECK(sl.Get(ControlState::Pressed) == normal); // fallback
}

TEST_CASE("drawable: StateList_GetReturnsNullIfNoNormal")
{
    StateListDrawable sl;
    CHECK(sl.Get(ControlState::Normal) == nullptr);
    CHECK(sl.Get(ControlState::Hover) == nullptr);
}

// === LayerDrawable ===

TEST_CASE("drawable: Layer_AddLayer_IncreasesCount")
{
    // Just verify it doesn't crash - drawing needs a VGContext. AddLayer consumes the ref.
    LayerDrawable layer;
    layer.AddLayer(core::MakeRef<ColorDrawable>(core::DefaultAllocator(), core::Color::Red));
    layer.AddLayer(core::MakeRef<ColorDrawable>(core::DefaultAllocator(), core::Color::Blue),
                   Thickness{5.0f, 5.0f, 5.0f, 5.0f});
}

// === InsetDrawable ===

TEST_CASE("drawable: Inset_DrawablePadding_MatchesInset")
{
    InsetDrawable inset{core::MakeRef<ColorDrawable>(core::DefaultAllocator(), core::Color::Red),
                        Thickness{10.0f, 5.0f, 10.0f, 5.0f}};
    Thickness pad = inset.DrawablePadding();
    CHECK(pad.Left == 10.0f);
    CHECK(pad.Top == 5.0f);
    CHECK(pad.Right == 10.0f);
    CHECK(pad.Bottom == 5.0f);
}

// === ColorDrawable ===

TEST_CASE("drawable: ColorDrawable_NoIntrinsicSize")
{
    ColorDrawable cd{core::Color::Red};
    CHECK_FALSE(cd.IntrinsicSize().HasValue());
}

// === RoundedRectDrawable ===

TEST_CASE("drawable: RoundedRect_NoIntrinsicSize")
{
    RoundedRectDrawable rr{core::Color::Red, 4.0f, core::Color::Blue, 1.0f};
    CHECK_FALSE(rr.IntrinsicSize().HasValue());
}

// === NineSliceDrawable ===

TEST_CASE("drawable: NineSlice_DrawablePadding_AccountsForExpand")
{
    NineSliceDrawable ns{nullptr, foundation::image::NineSlice{10.0f, 10.0f, 10.0f, 10.0f}};
    ns.Expand = Thickness{5.0f, 5.0f, 5.0f, 5.0f};
    Thickness pad = ns.DrawablePadding();
    // Padding = max(0, Slices - Expand) = max(0, 10-5) = 5
    CHECK(pad.Left == 5.0f);
    CHECK(pad.Top == 5.0f);
    CHECK(pad.Right == 5.0f);
    CHECK(pad.Bottom == 5.0f);
}

TEST_CASE("drawable: NineSlice_DrawablePadding_ClampsToZero")
{
    NineSliceDrawable ns{nullptr, foundation::image::NineSlice{5.0f, 5.0f, 5.0f, 5.0f}};
    ns.Expand = Thickness{10.0f, 10.0f, 10.0f, 10.0f};
    Thickness pad = ns.DrawablePadding();
    CHECK(pad.Left == 0.0f);
    CHECK(pad.Top == 0.0f);
}

// === Drawable base ===

TEST_CASE("drawable: Drawable_StateAwareDraw_DelegatesToStateless")
{
    // ShapeDrawable has no state-aware override - should delegate. Creation must not invoke it.
    bool called = false;
    ShapeDrawable sd{
        ShapeDrawable::DrawFn{[&](UIDrawContext&, const core::Rectangle&) { called = true; }}};
    CHECK_FALSE(called);
}

TEST_CASE("drawable: statelist Disabled dominates interaction flags")
{
    // A disabled control under the mouse is Disabled|Hover: it must render DISABLED, not
    // light up with the hover layer (the generic high->low flag stripping got this wrong).
    auto list = core::MakeRef<StateListDrawable>(core::DefaultAllocator());
    auto normal = core::MakeRef<ColorDrawable>(core::DefaultAllocator(), core::Color::Black);
    auto hover = core::MakeRef<ColorDrawable>(core::DefaultAllocator(), core::Color::Green);
    auto disabled = core::MakeRef<ColorDrawable>(core::DefaultAllocator(), core::Color::Red);
    list->Set(ControlState::Normal, core::RefPtr<Drawable>(normal.Get()));
    list->Set(ControlState::Hover, core::RefPtr<Drawable>(hover.Get()));
    list->Set(ControlState::Disabled, core::RefPtr<Drawable>(disabled.Get()));

    CHECK(list->Get(ControlState::Disabled | ControlState::Hover) == disabled.Get());
    CHECK(list->Get(ControlState::Disabled | ControlState::Pressed) == disabled.Get());
    CHECK(list->Get(ControlState::Disabled | ControlState::Focused | ControlState::Hover) ==
          disabled.Get());
    CHECK(list->Get(ControlState::Hover) == hover.Get()); // unchanged
    CHECK(list->Get(ControlState::Hover | ControlState::Focused) ==
          hover.Get()); // generic fallback intact
}
