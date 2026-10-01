// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// ScriptedInputSource (agent-playtesting-and-asset-creation.md P4): a timeline plays a virtual
// keyboard, mouse and gamepads in time order, each frame's edges and events its own, a press and
// its release in one frame still a press, a release-all frame at the end, and the cases parsed by
// their reflected names; an action map reads it as it reads a player's devices.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.shell;
import foundation.input;

using namespace foundation::core;
using namespace foundation::input;
namespace shell = foundation::shell;

namespace
{
    ScriptedInput KeyAt(f64 at, shell::KeyCode key, bool down)
    {
        ScriptedInput input;
        input.at = at;
        input.kind = ScriptedInputKind::Key;
        input.key = key;
        input.down = down;
        return input;
    }
}

TEST_CASE("scripted-input: a timeline applies in time order, each frame's edges and events its own")
{
    ScriptedInputSource source(DefaultAllocator());
    // Added out of order: kept in time order.
    REQUIRE(source.Add(KeyAt(0.5, shell::KeyCode::D, false)));
    REQUIRE(source.Add(KeyAt(0.0, shell::KeyCode::D, true)));
    CHECK(source.Count() == 2u);

    source.Advance(0.0);
    CHECK(source.Keyboard()->IsKeyDown(shell::KeyCode::D));
    CHECK(source.Keyboard()->IsKeyPressed(shell::KeyCode::D));
    REQUIRE(source.Events().Size() == 1u);
    CHECK(source.Events()[0].kind == shell::InputEventKind::KeyDown);

    // The next frame: still held, no edge, no event.
    source.Advance(0.25);
    CHECK(source.Keyboard()->IsKeyDown(shell::KeyCode::D));
    CHECK_FALSE(source.Keyboard()->IsKeyPressed(shell::KeyCode::D));
    CHECK(source.Events().IsEmpty());
    CHECK_FALSE(source.Finished());

    source.Advance(0.5);
    CHECK_FALSE(source.Keyboard()->IsKeyDown(shell::KeyCode::D));
    CHECK(source.Keyboard()->IsKeyReleased(shell::KeyCode::D));
    CHECK(source.Finished());
    CHECK(source.Time() == doctest::Approx(0.5));
}

TEST_CASE("scripted-input: a press and its release in one frame still read as a press")
{
    ScriptedInputSource source(DefaultAllocator());
    REQUIRE(source.Add(KeyAt(0.1, shell::KeyCode::Space, true)));
    REQUIRE(source.Add(KeyAt(0.1, shell::KeyCode::Space, false)));
    source.Advance(0.2);
    CHECK(source.Keyboard()->IsKeyPressed(shell::KeyCode::Space));
    CHECK(source.Keyboard()->IsKeyReleased(shell::KeyCode::Space));
    CHECK_FALSE(source.Keyboard()->IsKeyDown(shell::KeyCode::Space));
    CHECK(source.Events().Size() == 2u);
}

TEST_CASE("scripted-input: the mouse and the pads, a release-all frame, and the refusals")
{
    ScriptedInputSource source(DefaultAllocator());
    ScriptedInput move;
    move.kind = ScriptedInputKind::MouseMove;
    move.x = 100.0f;
    move.y = 40.0f;
    REQUIRE(source.Add(move));
    ScriptedInput click;
    click.kind = ScriptedInputKind::MouseButton;
    click.button = shell::MouseButton::Left;
    click.down = true;
    REQUIRE(source.Add(click));
    ScriptedInput pad;
    pad.kind = ScriptedInputKind::PadButton;
    pad.gamepad = 1;
    pad.padButton = shell::GamepadButton::South;
    pad.down = true;
    REQUIRE(source.Add(pad));
    ScriptedInput stick;
    stick.kind = ScriptedInputKind::PadAxis;
    stick.gamepad = 1;
    stick.padAxis = shell::GamepadAxis::LeftX;
    stick.value = 0.75f;
    REQUIRE(source.Add(stick));
    REQUIRE(source.Add(KeyAt(0.0, shell::KeyCode::LeftShift, true)));

    // A pad outside 0 to 3 is refused.
    ScriptedInput far = pad;
    far.gamepad = ScriptedInputSource::kMaxGamepads;
    CHECK_FALSE(source.Add(far));

    source.Advance(0.0);
    CHECK(source.Mouse()->X() == doctest::Approx(100.0f));
    CHECK(source.Mouse()->DeltaX() == doctest::Approx(100.0f));
    CHECK(source.Mouse()->IsButtonPressed(shell::MouseButton::Left));
    CHECK(source.GamepadCount() == 2); // the pads the timeline names: 0 and 1
    REQUIRE(source.Gamepad(1) != nullptr);
    CHECK(source.Gamepad(1)->IsButtonDown(shell::GamepadButton::South));
    CHECK(source.Gamepad(1)->Axis(shell::GamepadAxis::LeftX) == doctest::Approx(0.75f));
    CHECK(source.Gamepad(2) == nullptr);
    CHECK(static_cast<u32>(source.Keyboard()->Modifiers()) ==
          static_cast<u32>(shell::KeyModifiers::LeftShift));

    // Release all: everything held goes up, as a frame of its own.
    source.ReleaseAll();
    CHECK_FALSE(source.Mouse()->IsButtonDown(shell::MouseButton::Left));
    CHECK(source.Mouse()->IsButtonReleased(shell::MouseButton::Left));
    CHECK_FALSE(source.Gamepad(1)->IsButtonDown(shell::GamepadButton::South));
    CHECK(source.Gamepad(1)->Axis(shell::GamepadAxis::LeftX) == doctest::Approx(0.0f));
    CHECK_FALSE(source.Keyboard()->IsKeyDown(shell::KeyCode::LeftShift));
    CHECK(source.Events().Size() == 4u); // the button, the pad button, the axis, the key
}

TEST_CASE("scripted-input: the cases parse by their reflected names, any case")
{
    shell::KeyCode key{};
    CHECK(ScriptedInputSource::ParseKey(u8"space", key));
    CHECK(key == shell::KeyCode::Space);
    CHECK(ScriptedInputSource::ParseKey(u8"LeftShift", key));
    CHECK(key == shell::KeyCode::LeftShift);
    CHECK_FALSE(ScriptedInputSource::ParseKey(u8"Spacebar", key));
    shell::MouseButton button{};
    CHECK(ScriptedInputSource::ParseMouseButton(u8"right", button));
    CHECK(button == shell::MouseButton::Right);
    shell::GamepadButton padButton{};
    CHECK(ScriptedInputSource::ParsePadButton(u8"dpadup", padButton));
    CHECK(padButton == shell::GamepadButton::DPadUp);
    shell::GamepadAxis axis{};
    CHECK(ScriptedInputSource::ParsePadAxis(u8"RightTrigger", axis));
    CHECK(axis == shell::GamepadAxis::RightTrigger);
}

TEST_CASE("scripted-input: an action map reads the timeline as a player's devices")
{
    InputMap map;
    ActionSet set;
    set.name = String(u8"Gameplay");
    Action jump;
    jump.name = String(u8"Jump");
    jump.kind = ActionKind::Button;
    Binding key;
    key.source = BindingSource::Key;
    key.code = static_cast<u32>(shell::KeyCode::Space);
    jump.bindings.PushBack(key);
    set.actions.PushBack(static_cast<Action&&>(jump));
    map.sets.PushBack(static_cast<ActionSet&&>(set));

    ActionRuntime runtime;
    runtime.SetMap(map);
    ScriptedInputSource source(DefaultAllocator());
    REQUIRE(source.Add(KeyAt(0.1, shell::KeyCode::Space, true)));
    const ActionRef ref = runtime.Resolve(u8"Jump");
    source.Advance(0.0);
    runtime.Update(source, 1.0f / 60.0f);
    CHECK_FALSE(runtime.IsDown(ref));
    source.Advance(0.1);
    runtime.Update(source, 1.0f / 60.0f);
    CHECK(runtime.IsDown(ref));
}
