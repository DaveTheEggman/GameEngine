// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Input - :scripted_input partition (implementation).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module foundation.input;

import foundation.core;
import foundation.shell;

using namespace foundation::core;

namespace foundation::input
{
    namespace
    {
        template <typename E>
        bool InRange(E value, usize count) noexcept
        {
            return static_cast<usize>(value) < count;
        }

        /// A reflected enum case by name, any case (the cases come from the enum's reflection,
        /// never a list kept here).
        template <typename E>
        bool ParseCase(StringView name, E& out)
        {
            shell::RegisterShellInputReflection();
            const TypeInfo& type = TypeOf<E>();
            for (const EnumValue& value : Enumerators(type))
            {
                const StringView candidate(reinterpret_cast<const utf8char*>(value.name));
                if (candidate.Size() == name.Size() && candidate.ContainsIgnoreCase(name))
                {
                    out = static_cast<E>(value.value);
                    return true;
                }
            }
            return false;
        }
    }

    // ---- the virtual keyboard ----

    void ScriptedKeyboard::BeginFrame()
    {
        for (usize i = 0; i < kCount; ++i)
        {
            m_pressed[i] = false;
            m_released[i] = false;
        }
    }

    bool ScriptedKeyboard::Set(shell::KeyCode key, bool down)
    {
        const usize k = static_cast<usize>(key);
        if (k == 0 || k >= kCount || m_down[k] == down)
        {
            return false;
        }
        m_down[k] = down;
        (down ? m_pressed : m_released)[k] = true;
        return true;
    }

    bool ScriptedKeyboard::IsKeyDown(shell::KeyCode key) const
    {
        return InRange(key, kCount) && m_down[static_cast<usize>(key)];
    }
    bool ScriptedKeyboard::IsKeyPressed(shell::KeyCode key) const
    {
        return InRange(key, kCount) && m_pressed[static_cast<usize>(key)];
    }
    bool ScriptedKeyboard::IsKeyReleased(shell::KeyCode key) const
    {
        return InRange(key, kCount) && m_released[static_cast<usize>(key)];
    }

    shell::KeyModifiers ScriptedKeyboard::Modifiers() const
    {
        using shell::KeyCode;
        using shell::KeyModifiers;
        u32 m = 0;
        const auto add = [&](KeyCode key, KeyModifiers bit)
        {
            if (IsKeyDown(key))
            {
                m |= static_cast<u32>(bit);
            }
        };
        add(KeyCode::LeftShift, KeyModifiers::LeftShift);
        add(KeyCode::RightShift, KeyModifiers::RightShift);
        add(KeyCode::LeftCtrl, KeyModifiers::LeftCtrl);
        add(KeyCode::RightCtrl, KeyModifiers::RightCtrl);
        add(KeyCode::LeftAlt, KeyModifiers::LeftAlt);
        add(KeyCode::RightAlt, KeyModifiers::RightAlt);
        add(KeyCode::LeftGui, KeyModifiers::LeftGui);
        add(KeyCode::RightGui, KeyModifiers::RightGui);
        return static_cast<KeyModifiers>(m);
    }

    // ---- the virtual mouse ----

    void ScriptedMouse::BeginFrame()
    {
        for (usize i = 0; i < kCount; ++i)
        {
            m_pressed[i] = false;
            m_released[i] = false;
        }
        m_deltaX = m_deltaY = m_scrollX = m_scrollY = 0.0f;
    }

    bool ScriptedMouse::SetButton(shell::MouseButton button, bool down)
    {
        const usize b = static_cast<usize>(button);
        if (b >= kCount || m_down[b] == down)
        {
            return false;
        }
        m_down[b] = down;
        (down ? m_pressed : m_released)[b] = true;
        return true;
    }

    void ScriptedMouse::MoveTo(f32 x, f32 y)
    {
        m_deltaX += x - m_x;
        m_deltaY += y - m_y;
        m_x = x;
        m_y = y;
    }

    void ScriptedMouse::Wheel(f32 x, f32 y)
    {
        m_scrollX += x;
        m_scrollY += y;
    }

    bool ScriptedMouse::IsButtonDown(shell::MouseButton button) const
    {
        return InRange(button, kCount) && m_down[static_cast<usize>(button)];
    }
    bool ScriptedMouse::IsButtonPressed(shell::MouseButton button) const
    {
        return InRange(button, kCount) && m_pressed[static_cast<usize>(button)];
    }
    bool ScriptedMouse::IsButtonReleased(shell::MouseButton button) const
    {
        return InRange(button, kCount) && m_released[static_cast<usize>(button)];
    }

    // ---- the virtual gamepads ----

    void ScriptedGamepad::BeginFrame()
    {
        for (usize i = 0; i < kButtons; ++i)
        {
            m_pressed[i] = false;
            m_released[i] = false;
        }
    }

    bool ScriptedGamepad::SetButton(shell::GamepadButton button, bool isDown)
    {
        const usize b = static_cast<usize>(button);
        if (b >= kButtons || down[b] == isDown)
        {
            return false;
        }
        down[b] = isDown;
        (isDown ? m_pressed : m_released)[b] = true;
        return true;
    }

    void ScriptedGamepad::SetAxis(shell::GamepadAxis axis, f32 value)
    {
        if (InRange(axis, kAxes))
        {
            axes[static_cast<usize>(axis)] = value;
        }
    }

    bool ScriptedGamepad::IsButtonDown(shell::GamepadButton button) const
    {
        return InRange(button, kButtons) && down[static_cast<usize>(button)];
    }
    bool ScriptedGamepad::IsButtonPressed(shell::GamepadButton button) const
    {
        return InRange(button, kButtons) && m_pressed[static_cast<usize>(button)];
    }
    bool ScriptedGamepad::IsButtonReleased(shell::GamepadButton button) const
    {
        return InRange(button, kButtons) && m_released[static_cast<usize>(button)];
    }
    f32 ScriptedGamepad::Axis(shell::GamepadAxis axis) const
    {
        return InRange(axis, kAxes) ? axes[static_cast<usize>(axis)] : 0.0f;
    }

    // ---- the source ----

    ScriptedInputSource::ScriptedInputSource(IAllocator& allocator)
        : m_timeline(allocator), m_events(allocator)
    {
    }

    shell::IGamepad* ScriptedInputSource::Gamepad(i32 index)
    {
        return (index >= 0 && index < m_padCount) ? &m_pads[index] : nullptr;
    }

    bool ScriptedInputSource::Add(const ScriptedInput& input)
    {
        const bool pad = input.kind == ScriptedInputKind::PadButton ||
                         input.kind == ScriptedInputKind::PadAxis;
        if (pad && (input.gamepad < 0 || input.gamepad >= kMaxGamepads))
        {
            return false;
        }
        usize index = m_timeline.Size();
        while (index > 0 && m_timeline[index - 1].at > input.at)
        {
            --index;
        }
        m_timeline.Insert(index, input);
        if (pad)
        {
            m_padCount = Max(m_padCount, input.gamepad + 1);
        }
        return true;
    }

    void ScriptedInputSource::BeginFrame()
    {
        m_events.Clear();
        m_keyboard.BeginFrame();
        m_mouse.BeginFrame();
        for (ScriptedGamepad& pad : m_pads)
        {
            pad.BeginFrame();
        }
    }

    void ScriptedInputSource::Advance(f64 time)
    {
        m_time = time;
        BeginFrame();
        while (m_next < m_timeline.Size() && m_timeline[m_next].at <= time)
        {
            Apply(m_timeline[m_next]);
            ++m_next;
        }
    }

    void ScriptedInputSource::ReleaseAll()
    {
        BeginFrame();
        for (u32 k = 1; k < static_cast<u32>(shell::KeyCode::Count); ++k)
        {
            const auto key = static_cast<shell::KeyCode>(k);
            if (m_keyboard.IsKeyDown(key))
            {
                ScriptedInput input;
                input.kind = ScriptedInputKind::Key;
                input.key = key;
                Apply(input);
            }
        }
        for (u32 b = 0; b < static_cast<u32>(shell::MouseButton::Count); ++b)
        {
            const auto button = static_cast<shell::MouseButton>(b);
            if (m_mouse.IsButtonDown(button))
            {
                ScriptedInput input;
                input.kind = ScriptedInputKind::MouseButton;
                input.button = button;
                Apply(input);
            }
        }
        for (ScriptedGamepad& pad : m_pads)
        {
            for (usize b = 0; b < ScriptedGamepad::kButtons; ++b)
            {
                if (pad.down[b])
                {
                    ScriptedInput input;
                    input.kind = ScriptedInputKind::PadButton;
                    input.gamepad = pad.Index();
                    input.padButton = static_cast<shell::GamepadButton>(b);
                    Apply(input);
                }
            }
            for (usize a = 0; a < ScriptedGamepad::kAxes; ++a)
            {
                if (pad.axes[a] != 0.0f)
                {
                    ScriptedInput input;
                    input.kind = ScriptedInputKind::PadAxis;
                    input.gamepad = pad.Index();
                    input.padAxis = static_cast<shell::GamepadAxis>(a);
                    Apply(input);
                }
            }
        }
    }

    void ScriptedInputSource::Apply(const ScriptedInput& input)
    {
        shell::InputEvent event;
        switch (input.kind)
        {
        case ScriptedInputKind::Key:
            if (!m_keyboard.Set(input.key, input.down))
            {
                return;
            }
            event.kind = input.down ? shell::InputEventKind::KeyDown : shell::InputEventKind::KeyUp;
            event.key = input.key;
            event.modifiers = m_keyboard.Modifiers();
            break;
        case ScriptedInputKind::MouseButton:
            if (!m_mouse.SetButton(input.button, input.down))
            {
                return;
            }
            event.kind = input.down ? shell::InputEventKind::MouseButtonDown
                                    : shell::InputEventKind::MouseButtonUp;
            event.button = input.button;
            event.x = m_mouse.X();
            event.y = m_mouse.Y();
            break;
        case ScriptedInputKind::MouseMove:
            event.kind = shell::InputEventKind::MouseMove;
            event.dx = input.x - m_mouse.X();
            event.dy = input.y - m_mouse.Y();
            m_mouse.MoveTo(input.x, input.y);
            event.x = input.x;
            event.y = input.y;
            break;
        case ScriptedInputKind::MouseWheel:
            m_mouse.Wheel(input.x, input.y);
            event.kind = shell::InputEventKind::MouseWheel;
            event.x = input.x;
            event.y = input.y;
            break;
        case ScriptedInputKind::PadButton:
            if (!m_pads[input.gamepad].SetButton(input.padButton, input.down))
            {
                return;
            }
            event.kind = input.down ? shell::InputEventKind::GamepadButtonDown
                                    : shell::InputEventKind::GamepadButtonUp;
            event.gamepad = input.gamepad;
            event.padButton = input.padButton;
            break;
        case ScriptedInputKind::PadAxis:
            m_pads[input.gamepad].SetAxis(input.padAxis, input.value);
            event.kind = shell::InputEventKind::GamepadAxis;
            event.gamepad = input.gamepad;
            event.padAxis = input.padAxis;
            event.value = input.value;
            break;
        }
        m_events.PushBack(event);
    }

    bool ScriptedInputSource::ParseKey(StringView name, shell::KeyCode& out)
    {
        return ParseCase(name, out);
    }
    bool ScriptedInputSource::ParseMouseButton(StringView name, shell::MouseButton& out)
    {
        return ParseCase(name, out);
    }
    bool ScriptedInputSource::ParsePadButton(StringView name, shell::GamepadButton& out)
    {
        return ParseCase(name, out);
    }
    bool ScriptedInputSource::ParsePadAxis(StringView name, shell::GamepadAxis& out)
    {
        return ParseCase(name, out);
    }
}
