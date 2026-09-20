// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The input map editor's registration and its headless edits: what the rows' clicks and the
// listen capture do to the map, without a page. Ported back from the Beef port's
// Editor.Input.Tests (2026-09-20).
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.runtime;
import foundation.runtime.client;
import foundation.shell;
import foundation.graphics;
import foundation.input;
import input.pipeline;
import editor.core;
import editor.input;

using namespace foundation::core;
namespace input = foundation::input;
namespace runtime = foundation::runtime;
using namespace editor::input_map_edit;

namespace
{
    // Headless host stub: the registration only stores it.
    class StubHost final : public runtime::IApplicationHost
    {
    public:
        runtime::Context& Ctx() noexcept override { return m_context; }
        foundation::shell::IShell* Shell() noexcept override { return nullptr; }
        foundation::graphics::GraphicsDevice* Graphics() noexcept override { return nullptr; }
        foundation::graphics::RenderWindow* MainRenderWindow() noexcept override { return nullptr; }
        foundation::graphics::RenderWindow*
        OpenWindow(const foundation::shell::WindowSettings&,
                   const foundation::graphics::RenderWindowDesc&) override
        {
            return nullptr;
        }
        void CloseWindow(foundation::graphics::RenderWindow*) override {}
        void RequestExit(int) override {}

    private:
        runtime::Context m_context{DefaultAllocator()};
    };
}

TEST_CASE("input editor: registering routes the input map asset to its factory")
{
    editor::EditorContext context{DefaultAllocator()};
    StubHost host;
    editor::RegisterInputEditor(context, host);
    editor::IEditorPageFactory* found =
        context.Pages().FindFactory(pipeline::InputMapAsset::StaticType());
    REQUIRE(found != nullptr);
    CHECK(found->PrimaryType() == &pipeline::InputMapAsset::StaticType());
}

TEST_CASE("input editor: lookups are guarded and a fresh binding follows the action's kind")
{
    pipeline::InputMapAsset asset;
    asset.SeedDefaultContent();
    input::InputMap& map = asset.Map();
    CHECK(SetAt(map, 0) != nullptr);
    CHECK(SetAt(map, 1) == nullptr);
    REQUIRE(ActionAt(map, 0, 3) != nullptr);
    CHECK(ActionAt(map, 0, 3)->name == StringView(u8"Fire"));
    CHECK(ActionAt(map, 0, 4) == nullptr);
    CHECK_FALSE(HasBinding(map, 0, 0, 0));

    input::Action* move = ActionAt(map, 0, 0);
    REQUIRE(move != nullptr);
    move->bindings.PushBack(FreshBinding(move->kind));
    CHECK(HasBinding(map, 0, 0, 0));
    CHECK(move->bindings[0].source == input::BindingSource::GamepadStick); // a 2D axis wants a stick
    CHECK(FreshBinding(input::ActionKind::Button).source == input::BindingSource::Key);
}

TEST_CASE("input editor: cycling the source walks the kind's valid list on a fresh binding")
{
    input::BindingSource valid[8];
    const usize n = input::ValidSources(input::ActionKind::Button, valid);
    REQUIRE(n >= 2u);

    input::Binding binding;
    binding.source = valid[0];
    binding.scale = 7.0f;
    const input::Binding next = CycleSource(input::ActionKind::Button, binding);
    CHECK(next.source == valid[1]);
    CHECK(next.scale == 1.0f); // the source specifics reset

    input::Binding last;
    last.source = valid[n - 1];
    CHECK(CycleSource(input::ActionKind::Button, last).source == valid[0]); // wraps
}

TEST_CASE("input editor: the listen filter follows the action's kind")
{
    const input::CaptureFilter button = FilterFor(input::ActionKind::Button, false);
    CHECK((button.keys && button.mouseButtons && button.gamepadButtons));
    CHECK((!button.gamepadAxes && !button.gamepadSticks));

    const input::CaptureFilter axis = FilterFor(input::ActionKind::Axis1D, false);
    CHECK((axis.keys && axis.gamepadAxes && !axis.gamepadSticks));

    const input::CaptureFilter stick = FilterFor(input::ActionKind::Axis2D, false);
    CHECK((!stick.keys && !stick.mouseButtons && !stick.gamepadButtons && stick.gamepadSticks));

    // One composite direction is always a single key, whatever the kind.
    const input::CaptureFilter direction = FilterFor(input::ActionKind::Axis2D, true);
    CHECK((direction.keys && !direction.mouseButtons && !direction.gamepadButtons));
}

TEST_CASE("input editor: a capture lands in the binding, or in one composite direction")
{
    pipeline::InputMapAsset asset;
    asset.SeedDefaultContent();
    input::InputMap& map = asset.Map();

    input::Action* jump = ActionAt(map, 0, 2);
    REQUIRE(jump != nullptr);
    jump->bindings.PushBack(FreshBinding(input::ActionKind::Button));
    input::Binding captured;
    captured.source = input::BindingSource::MouseButton;
    captured.code = 3;
    ApplyCapture(map, 0, 2, 0, -1, captured);
    CHECK(jump->bindings[0].source == input::BindingSource::MouseButton);
    CHECK(jump->bindings[0].code == 3u);

    input::Action* move = ActionAt(map, 0, 0);
    REQUIRE(move != nullptr);
    input::Binding composite;
    composite.source = input::BindingSource::Composite2D;
    move->bindings.PushBack(composite);
    input::Binding key;
    key.code = 42;
    ApplyCapture(map, 0, 0, 0, 3, key); // +Y
    CHECK(move->bindings[0].source == input::BindingSource::Composite2D);
    CHECK(move->bindings[0].posY == 42u);
    CHECK(move->bindings[0].negX == 0u);

    // Out-of-range targets are ignored.
    ApplyCapture(map, 0, 0, 5, -1, key);
    ApplyCapture(map, 9, 0, 0, -1, key);
    CHECK(move->bindings.Size() == 1u);
}
