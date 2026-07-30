// Draconic::ShellWeb - `draconic.shell.web:input`.
//
// The web shell's input devices. Stubbed for now - they report nothing held/pressed so callers use
// Input() uniformly without null checks. This is where the HTML5 wiring lands later
// (emscripten_set_keydown_callback / mousemove / wheel / touch feeding these classes), which is why
// the web shell keeps its own devices rather than borrowing the Null shell's.

module;
#include "Core/Prelude.h"

export module draconic.shell.web:input;

import draconic.core;
import draconic.shell;

namespace core = draconic::core;

export namespace draconic::shell
{
    class WebKeyboard final : public IKeyboard
    {
    public:
        [[nodiscard]] bool IsKeyDown(KeyCode) const override { return false; }
        [[nodiscard]] bool IsKeyPressed(KeyCode) const override { return false; }
        [[nodiscard]] bool IsKeyReleased(KeyCode) const override { return false; }
        [[nodiscard]] KeyModifiers Modifiers() const override { return KeyModifiers::None; }
    };

    class WebMouse final : public IMouse
    {
    public:
        [[nodiscard]] core::f32 X() const override { return 0.0f; }
        [[nodiscard]] core::f32 Y() const override { return 0.0f; }
        [[nodiscard]] core::f32 GlobalX() const override { return 0.0f; }
        [[nodiscard]] core::f32 GlobalY() const override { return 0.0f; }
        [[nodiscard]] core::f32 DeltaX() const override { return 0.0f; }
        [[nodiscard]] core::f32 DeltaY() const override { return 0.0f; }
        [[nodiscard]] core::f32 ScrollX() const override { return 0.0f; }
        [[nodiscard]] core::f32 ScrollY() const override { return 0.0f; }
        [[nodiscard]] bool IsButtonDown(MouseButton) const override { return false; }
        [[nodiscard]] bool IsButtonPressed(MouseButton) const override { return false; }
        [[nodiscard]] bool IsButtonReleased(MouseButton) const override { return false; }
        [[nodiscard]] bool RelativeMode() const override { return false; }
        void SetRelativeMode(bool) override {}
        [[nodiscard]] bool CursorVisible() const override { return true; }
        void SetCursorVisible(bool) override {}
        void SetCursor(CursorType) override {}
        void SetGlobalCapture(bool) override {}
    };

    class WebTouch final : public ITouch
    {
    public:
        [[nodiscard]] core::i32 TouchCount() const override { return 0; }
        [[nodiscard]] bool GetTouchPoint(core::i32, TouchPoint&) const override { return false; }
        [[nodiscard]] bool HasTouch() const override { return false; }
    };

    class WebInputManager final : public IInputManager
    {
    public:
        [[nodiscard]] IKeyboard* Keyboard() override { return &m_keyboard; }
        [[nodiscard]] IMouse* Mouse() override { return &m_mouse; }
        [[nodiscard]] ITouch* Touch() override { return &m_touch; }
        [[nodiscard]] core::i32 GamepadCount() const override { return 0; }
        [[nodiscard]] IGamepad* GetGamepad(core::i32) override { return nullptr; }
        [[nodiscard]] core::Span<const InputEvent> Events() const override { return {}; }
        [[nodiscard]] core::u32 HoverWindow() const override { return 0; }
        [[nodiscard]] core::u32 FocusedWindow() const override { return 0; }
        void Update() override {}

    private:
        WebKeyboard m_keyboard;
        WebMouse m_mouse;
        WebTouch m_touch;
    };
}
