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

// Sedulous 3bffde51: a view's opacity at once, or faded on the UI's frame clock (which runs while
// the game is paused); a set stops a running fade; a null handle takes nothing.
TEST_CASE("uiscript.handle: a view fades on the frame clock, and a set stops the fade")
{
    ui::UIContext ctx{DefaultAllocator()};
    auto root = MakeRef<ui::RootView>(DefaultAllocator());
    root->ViewportSize = Float2{800.0f, 600.0f};
    ctx.AddRootView(root.Get());
    auto panel = MakeRef<ui::FrameLayout>(DefaultAllocator());
    panel->Name = String(u8"panel");
    root->AddView(panel.Get());
    panel->AddView(MakeLabel(u8"title", u8"Paused").Get());
    uis::ViewGroup rootGroup = Group(root.Get());

    uis::Label label = rootGroup.findLabel(u8"title");
    REQUIRE(label.isValid());
    CHECK(label.opacity() == doctest::Approx(1.0f));
    label.setOpacity(0.25f);
    CHECK(label.opacity() == doctest::Approx(0.25f));
    label.setOpacity(3.0f);
    CHECK(label.opacity() == doctest::Approx(1.0f)); // clamped

    label.fadeTo(0.0f, 1.0f);
    CHECK(label.opacity() == doctest::Approx(1.0f)); // from where it was
    ctx.BeginFrame(0.5f);
    CHECK(label.opacity() > 0.0f);
    CHECK(label.opacity() < 1.0f); // half way
    ctx.BeginFrame(0.6f);
    CHECK(label.opacity() == doctest::Approx(0.0f));

    label.fadeTo(1.0f, 1.0f);
    ctx.BeginFrame(0.2f);
    label.setOpacity(0.5f);
    ctx.BeginFrame(1.0f);
    CHECK(label.opacity() == doctest::Approx(0.5f)); // the set stopped the fade

    // Zero seconds is a set; a group fades too; a null handle takes nothing.
    label.fadeTo(0.1f, 0.0f);
    CHECK(label.opacity() == doctest::Approx(0.1f));
    uis::ViewGroup group = rootGroup.findGroup(u8"panel");
    REQUIRE(group.isValid());
    group.fadeTo(0.0f, 0.2f);
    ctx.BeginFrame(0.3f);
    CHECK(group.opacity() == doctest::Approx(0.0f));
    uis::Label missing = rootGroup.findLabel(u8"nope");
    missing.setOpacity(0.5f);
    missing.fadeTo(0.5f, 1.0f);
    CHECK(missing.opacity() == doctest::Approx(0.0f));
}

// A minimap marker moves and turns without a relayout: the handle writes the view's post-layout
// transform (translation in pixels, rotation in degrees stored as radians); a null handle takes
// nothing.
TEST_CASE("uiscript.handle: a view is translated and rotated through its transform")
{
    auto group = MakeRef<ui::FrameLayout>(DefaultAllocator());
    auto marker = MakeLabel(u8"marker", u8"^");
    group->AddView(marker.Get());
    uis::ViewGroup g = Group(group.Get());

    uis::Label handle = g.findLabel(u8"marker");
    REQUIRE(handle.isValid());
    CHECK(handle.translation().x == doctest::Approx(0.0f));
    CHECK(handle.rotation() == doctest::Approx(0.0f));

    handle.setTranslation(40.0f, -12.5f);
    handle.setRotation(90.0f);
    CHECK(marker->Transform.Translation.x == doctest::Approx(40.0f));
    CHECK(marker->Transform.Translation.y == doctest::Approx(-12.5f));
    CHECK(marker->Transform.Rotation == doctest::Approx(DegreesToRadians(90.0f)));
    CHECK(handle.translation().y == doctest::Approx(-12.5f));
    CHECK(handle.rotation() == doctest::Approx(90.0f));

    // Every handle type has it: the bare view the generic finder returns moves the same way.
    uis::View bare = g.find(u8"marker");
    REQUIRE(bare.isValid());
    bare.setTranslation(1.0f, 2.0f);
    CHECK(marker->Transform.Translation.x == doctest::Approx(1.0f));

    uis::Label missing = g.findLabel(u8"nope");
    missing.setTranslation(5.0f, 5.0f);
    missing.setRotation(45.0f);
    CHECK(missing.translation().x == doctest::Approx(0.0f));
    CHECK(missing.rotation() == doctest::Approx(0.0f));
}
