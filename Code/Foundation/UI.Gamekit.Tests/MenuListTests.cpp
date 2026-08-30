// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI.Gamekit - MenuList behavior (native, backend-neutral).
//
// A MenuList is a vertical FlexLayout of focusable Button rows. These cover row creation, the
// wrap-around Up/Down directional-focus chain, activation (callback + OnItemActivated), and
// selection-follows-CORE-focus (SetSelectedIndex / FocusFirst through the context's FocusManager).
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
    struct Bed
    {
        UIContext context;
        RefPtr<RootView> root;
        RefPtr<MenuList> menu;
        Bed()
        {
            root = MakeRef<RootView>(DefaultAllocator());
            context.AddRootView(root.Get());
            menu = MakeRef<MenuList>(DefaultAllocator());
            root->AddView(menu.Get()); // attaches menu (+ its rows) to the context
        }
    };
}

TEST_CASE("menulist: AddItem appends focusable rows")
{
    Bed bed;
    CHECK(bed.menu->ItemCount() == 0);
    Button* a = bed.menu->AddItem(u8"Start");
    Button* b = bed.menu->AddItem(u8"Options");
    Button* c = bed.menu->AddItem(u8"Quit");

    CHECK(bed.menu->ItemCount() == 3);
    CHECK(bed.menu->ItemAt(0) == a);
    CHECK(bed.menu->ItemAt(2) == c);
    CHECK(bed.menu->ItemAt(3) == nullptr); // out of range
    CHECK(bed.menu->ChildCount() == 3);    // rows are the menu's children
    CHECK(a->IsFocusable);
    CHECK(b->IsFocusable);
    CHECK(c->IsFocusable);
}

TEST_CASE("menulist: Up/Down focus chain wraps around")
{
    Bed bed;
    Button* a = bed.menu->AddItem(u8"A");
    Button* b = bed.menu->AddItem(u8"B");
    Button* c = bed.menu->AddItem(u8"C");

    // Down: A -> B -> C -> A (wrap); Up: A -> C (wrap), B -> A, C -> B.
    REQUIRE(a->NextFocusDown.HasValue());
    CHECK(a->NextFocusDown.Value() == b->Id);
    CHECK(b->NextFocusDown.Value() == c->Id);
    CHECK(c->NextFocusDown.Value() == a->Id);
    CHECK(a->NextFocusUp.Value() == c->Id);
    CHECK(b->NextFocusUp.Value() == a->Id);
    CHECK(c->NextFocusUp.Value() == b->Id);
}

TEST_CASE("menulist: activating a row fires its callback + OnItemActivated with the index")
{
    Bed bed;
    int picked = -1;
    i32 activatedIndex = -1;
    bed.menu->OnItemActivated.Add([&](MenuList*, i32 i) { activatedIndex = i; });
    bed.menu->AddItem(u8"First", [&picked]() { picked = 0; });
    Button* second = bed.menu->AddItem(u8"Second", [&picked]() { picked = 1; });

    second->FireClick();
    CHECK(picked == 1);
    CHECK(activatedIndex == 1);
}

TEST_CASE("menulist: selection follows CORE focus (SetSelectedIndex / FocusFirst)")
{
    Bed bed;
    bed.menu->AddItem(u8"A");
    bed.menu->AddItem(u8"B");
    bed.menu->AddItem(u8"C");

    CHECK(bed.menu->SelectedIndex() == -1); // nothing focused yet
    bed.menu->SetSelectedIndex(1);
    CHECK(bed.menu->SelectedIndex() == 1);
    bed.menu->FocusFirst();
    CHECK(bed.menu->SelectedIndex() == 0);
    bed.menu->SetSelectedIndex(99); // out of range: no-op, selection unchanged
    CHECK(bed.menu->SelectedIndex() == 0);
}

TEST_CASE("menulist: ClearItems empties the list")
{
    Bed bed;
    bed.menu->AddItem(u8"A");
    bed.menu->AddItem(u8"B");
    CHECK(bed.menu->ItemCount() == 2);

    bed.menu->ClearItems();
    CHECK(bed.menu->ItemCount() == 0);
    CHECK(bed.menu->ChildCount() == 0);
}
