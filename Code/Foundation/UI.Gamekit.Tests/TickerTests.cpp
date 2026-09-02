// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI.Gamekit - Ticker behavior (native, backend-neutral).
//
// A Ticker is a Label showing an integer that animates to new values. These cover the instant
// SetNumber (value + rendered text), the animated AnimateTo rolling across frames and landing exactly
// on the target, and the duration<=0 snap.
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
    struct TickerBed
    {
        UIContext context{DefaultAllocator()};
        RefPtr<RootView> root;
        RefPtr<Ticker> ticker;
        TickerBed()
        {
            root = MakeRef<RootView>(DefaultAllocator());
            context.AddRootView(root.Get());
            ticker = MakeRef<Ticker>(DefaultAllocator());
            root->AddView(ticker.Get());
        }
    };
}

TEST_CASE("ticker: SetNumber updates the value + rendered text")
{
    TickerBed bed;
    CHECK(bed.ticker->Number() == 0);
    CHECK(bed.ticker->Text.Value() == StringView(u8"0"));

    bed.ticker->SetNumber(1234);
    CHECK(bed.ticker->Number() == 1234);
    CHECK(bed.ticker->Text.Value() == StringView(u8"1234"));

    bed.ticker->SetNumber(-7);
    CHECK(bed.ticker->Text.Value() == StringView(u8"-7"));
}

TEST_CASE("ticker: AnimateTo rolls to the target over frames, landing exactly")
{
    TickerBed bed;
    bed.ticker->SetNumber(0);
    bed.ticker->AnimateTo(100, 1.0f);

    bed.context.BeginFrame(0.5f); // partway up
    const i64 mid = bed.ticker->Number();
    CHECK(mid > 0);
    CHECK(mid < 100);

    bed.context.BeginFrame(0.6f); // past the duration
    CHECK(bed.ticker->Number() == 100);
    CHECK(bed.ticker->Text.Value() == StringView(u8"100"));
}

TEST_CASE("ticker: AnimateTo with duration 0 snaps immediately")
{
    TickerBed bed;
    bed.ticker->AnimateTo(42, 0.0f);
    CHECK(bed.ticker->Number() == 42);
    CHECK(bed.ticker->Text.Value() == StringView(u8"42"));
}
