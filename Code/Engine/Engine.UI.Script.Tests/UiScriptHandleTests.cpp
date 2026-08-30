// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// engine.ui.script - the reflected view handles (native, no VM).
//
// These prove the properties the scripted parity suite relies on but cannot express: the OWNING
// handle keeps a view alive after its screen is dropped (no use-after-free - Fable finding B), and the
// typed finders are loud-null on a missing name OR a type mismatch, with ops on a null handle safe.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import engine.ui.script;

using namespace foundation::core;
namespace ui = foundation::ui;
namespace uis = engine::uiscript;

namespace
{
    // Wrap a borrowed view in a group handle (the finders live on it).
    [[nodiscard]] uis::ViewGroup Group(ui::ViewGroup* g)
    {
        uis::ViewGroup h;
        h.view = RefPtr<ui::View>(g);
        return h;
    }

    [[nodiscard]] RefPtr<ui::Label> MakeLabel(StringView name, StringView text)
    {
        auto l = MakeRef<ui::Label>(DefaultAllocator());
        l->Name = String(name);
        l->Text.SetValue(String(text));
        return l;
    }
}

TEST_CASE("uiscript.handle: an owning handle keeps its view alive after the tree drops it")
{
    auto group = MakeRef<ui::FrameLayout>(DefaultAllocator());
    auto label = MakeLabel(u8"status", u8"Idle");
    group->AddView(label.Get());

    // A script would hold this handle (e.g. a cached HUD label). It owns a ref.
    uis::Label held = Group(group.Get()).findLabel(u8"status");
    REQUIRE(held.isValid());

    // The screen is popped: drop the tree's + our local strong refs. The handle's RefPtr is the last
    // owner, so the label stays alive - ops must be safe, never a UAF.
    group->RemoveView(label.Get());
    group = nullptr;
    label = nullptr;

    CHECK(held.isValid());
    held.setText(u8"Loading"); // safe: operates on the detached-but-alive view
    CHECK(held.text() == StringView(u8"Loading"));
}

TEST_CASE("uiscript.handle: typed finders are loud-null on missing name or type mismatch")
{
    auto group = MakeRef<ui::FrameLayout>(DefaultAllocator());
    group->AddView(MakeLabel(u8"status", u8"Idle").Get());
    auto button = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Go"));
    button->Name = String(u8"go");
    group->AddView(button.Get());

    uis::ViewGroup g = Group(group.Get());

    CHECK(g.findLabel(u8"status").isValid());   // present + right type
    CHECK_FALSE(g.findLabel(u8"go").isValid());  // present but a Button -> loud null
    CHECK_FALSE(g.findLabel(u8"nope").isValid()); // absent -> loud null
    CHECK(g.findButton(u8"go").isValid());
    CHECK_FALSE(g.findButton(u8"status").isValid());

    // Ops on a null handle are safe no-ops (no crash, nothing happens).
    uis::Label absent = g.findLabel(u8"nope");
    absent.setText(u8"ignored");
    CHECK(absent.text() == StringView(u8""));
}

TEST_CASE("uiscript.handle: findLabel searches the subtree deeply, first match")
{
    auto root = MakeRef<ui::FrameLayout>(DefaultAllocator());
    auto mid = MakeRef<ui::FrameLayout>(DefaultAllocator());
    auto inner = MakeRef<ui::FrameLayout>(DefaultAllocator());
    root->AddView(mid.Get());
    mid->AddView(inner.Get());
    inner->AddView(MakeLabel(u8"deep", u8"found").Get());

    uis::Label deep = Group(root.Get()).findLabel(u8"deep");
    REQUIRE(deep.isValid());
    CHECK(deep.text() == StringView(u8"found"));
}
