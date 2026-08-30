// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI.Gamekit - Bar behavior (native, backend-neutral).
//
// A Bar is a ProgressBar with an optional smooth drain. These cover the instant SetFill (with the
// inherited 0..1 clamp), the animated AnimateTo draining toward its target across frames (pumped via
// UIContext::BeginFrame), and the duration<=0 snap path.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import foundation.ui.gamekit;

using namespace foundation::core;
using namespace foundation::ui;
using namespace foundation::ui::gamekit;

namespace
{
    struct BarBed
    {
        UIContext context;
        RefPtr<RootView> root;
        RefPtr<Bar> bar;
        BarBed()
        {
            root = MakeRef<RootView>(DefaultAllocator());
            context.AddRootView(root.Get());
            bar = MakeRef<Bar>(DefaultAllocator());
            root->AddView(bar.Get()); // attaches the bar to the context (for Animations())
        }
    };
}

TEST_CASE("bar: SetFill snaps + clamps to [0..1]")
{
    BarBed bed;
    bed.bar->SetFill(0.5f);
    CHECK(bed.bar->Value.Value() == doctest::Approx(0.5f));
    bed.bar->SetFill(2.0f); // over-range clamps (ProgressBar)
    CHECK(bed.bar->Value.Value() == doctest::Approx(1.0f));
    bed.bar->SetFill(-1.0f);
    CHECK(bed.bar->Value.Value() == doctest::Approx(0.0f));
}

TEST_CASE("bar: AnimateTo drains toward the target across frames, arriving exactly")
{
    BarBed bed;
    bed.bar->SetFill(1.0f);
    bed.bar->AnimateTo(0.0f, 1.0f); // full -> empty over 1s

    // Half-way through the duration the fill is partway down (linear ease -> ~0.5).
    bed.context.BeginFrame(0.5f);
    const f32 mid = bed.bar->Value.Value();
    CHECK(mid > 0.0f);
    CHECK(mid < 1.0f);

    // Past the full duration it arrives exactly at the target.
    bed.context.BeginFrame(0.6f); // elapsed 1.1s > 1.0s
    CHECK(bed.bar->Value.Value() == doctest::Approx(0.0f));
}

TEST_CASE("bar: a new AnimateTo replaces the in-flight drain (no stacking)")
{
    BarBed bed;
    bed.bar->SetFill(1.0f);
    bed.bar->AnimateTo(0.0f, 1.0f);
    bed.context.BeginFrame(0.5f); // ~0.5
    bed.bar->AnimateTo(1.0f, 1.0f); // reverse: from ~0.5 back up to 1
    bed.context.BeginFrame(1.1f);   // complete the second animation
    CHECK(bed.bar->Value.Value() == doctest::Approx(1.0f));
}

TEST_CASE("bar: AnimateTo with duration 0 snaps immediately")
{
    BarBed bed;
    bed.bar->SetFill(1.0f);
    bed.bar->AnimateTo(0.25f, 0.0f);
    CHECK(bed.bar->Value.Value() == doctest::Approx(0.25f));
}
