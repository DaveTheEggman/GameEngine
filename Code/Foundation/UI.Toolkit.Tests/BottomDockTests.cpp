// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// BottomDock tests: the Godot-style collapsible bottom strip. Headless - exercises the tab/expand
// state machine + OnExpandedChanged event + per-tab content visibility (the host wires the event to
// SplitView::SetPaneCollapsed; that half is covered by SplitViewTests).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;

using namespace foundation::ui;
using namespace foundation::ui::toolkit;
using namespace foundation::core;
namespace core = foundation::core;

TEST_CASE("toolkit-bottomdock: starts collapsed; clicking a tab expands, clicking it again collapses")
{
    auto dock = core::MakeRef<BottomDock>(core::DefaultAllocator());
    auto anim = core::MakeRef<Panel>(core::DefaultAllocator());
    dock->AddTab(u8"animation", u8"Animation", anim.Get());
    CHECK(dock->TabCount() == 1u);

    // Default state: collapsed, content hidden.
    CHECK_FALSE(dock->IsExpanded());
    CHECK(anim->Visibility == Visibility::Gone);

    int events = 0;
    bool lastExpanded = false;
    dock->OnExpandedChanged.Add(
        [&](bool e)
        {
            ++events;
            lastExpanded = e;
        });

    // Click the tab -> expands, content shown, event(true), active id set.
    dock->ClickTab(u8"animation");
    CHECK(dock->IsExpanded());
    CHECK(anim->Visibility == Visibility::Visible);
    CHECK(dock->ActiveTabId() == StringView(u8"animation"));
    CHECK(events == 1);
    CHECK(lastExpanded == true);

    // Click the SAME (active) tab -> collapses back to just the bar, content hidden, event(false).
    dock->ClickTab(u8"animation");
    CHECK_FALSE(dock->IsExpanded());
    CHECK(anim->Visibility == Visibility::Gone);
    CHECK(events == 2);
    CHECK(lastExpanded == false);
}

TEST_CASE("toolkit-bottomdock: switching tabs keeps expanded and shows only the active content")
{
    auto dock = core::MakeRef<BottomDock>(core::DefaultAllocator());
    auto a = core::MakeRef<Panel>(core::DefaultAllocator());
    auto b = core::MakeRef<Panel>(core::DefaultAllocator());
    dock->AddTab(u8"a", u8"A", a.Get());
    dock->AddTab(u8"b", u8"B", b.Get());

    dock->ClickTab(u8"a");
    CHECK(dock->IsExpanded());
    CHECK(a->Visibility == Visibility::Visible);
    CHECK(b->Visibility == Visibility::Gone);

    // Clicking the OTHER tab switches (still expanded), only B visible now.
    dock->ClickTab(u8"b");
    CHECK(dock->IsExpanded());
    CHECK(dock->ActiveTabId() == StringView(u8"b"));
    CHECK(a->Visibility == Visibility::Gone);
    CHECK(b->Visibility == Visibility::Visible);

    // Collapsing hides all tab content.
    dock->SetExpanded(false);
    CHECK_FALSE(dock->IsExpanded());
    CHECK(a->Visibility == Visibility::Gone);
    CHECK(b->Visibility == Visibility::Gone);
}

TEST_CASE("toolkit-bottomdock: ActivateTab expands the named tab")
{
    auto dock = core::MakeRef<BottomDock>(core::DefaultAllocator());
    auto a = core::MakeRef<Panel>(core::DefaultAllocator());
    dock->AddTab(u8"a", u8"A", a.Get());

    dock->ActivateTab(u8"nope"); // unknown id -> no change
    CHECK_FALSE(dock->IsExpanded());

    dock->ActivateTab(u8"a");
    CHECK(dock->IsExpanded());
    CHECK(dock->ActiveTabId() == StringView(u8"a"));
    CHECK(a->Visibility == Visibility::Visible);
}
