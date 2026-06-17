// Raptor::RuntimePlatformNull — the `raptor.runtime.platform.null` module.
//
// A headless IPlatform implementation: no real window or OS events. Useful for
// tests, tools, and headless servers, and as the reference for what a real
// backend must provide. ProcessEvents is a no-op; IsRunning stays true until
// RequestExit (or the window is closed), so the runner relies on the
// Application requesting exit to terminate.

module;
#include "Core/Prelude.h"

export module raptor.runtime.platform.null;

import raptor.core;
import raptor.runtime.platform;

namespace rc = raptor::core;

export namespace raptor::runtime
{
    class NullWindow final : public IWindow
    {
    public:
        explicit NullWindow(const WindowSettings& settings) noexcept
            : m_width(settings.width), m_height(settings.height) {}

        [[nodiscard]] rc::u32 Width() const noexcept override { return m_width; }
        [[nodiscard]] rc::u32 Height() const noexcept override { return m_height; }
        [[nodiscard]] NativeWindow Native() const noexcept override { return {}; }  // headless: no handles
        [[nodiscard]] bool IsOpen() const noexcept override { return m_open; }
        [[nodiscard]] bool IsMinimized() const noexcept override { return false; }
        void Close() override { m_open = false; }

    private:
        rc::u32 m_width;
        rc::u32 m_height;
        bool m_open = true;
    };

    // No-op input devices: report nothing held/pressed so headless callers can
    // use Input() uniformly without null checks.
    class NullKeyboard final : public IKeyboard
    {
    public:
        [[nodiscard]] bool IsKeyDown(KeyCode) const override { return false; }
        [[nodiscard]] bool IsKeyPressed(KeyCode) const override { return false; }
        [[nodiscard]] bool IsKeyReleased(KeyCode) const override { return false; }
        [[nodiscard]] KeyModifiers Modifiers() const override { return KeyModifiers::None; }
    };

    class NullMouse final : public IMouse
    {
    public:
        [[nodiscard]] rc::f32 X() const override { return 0.0f; }
        [[nodiscard]] rc::f32 Y() const override { return 0.0f; }
        [[nodiscard]] rc::f32 DeltaX() const override { return 0.0f; }
        [[nodiscard]] rc::f32 DeltaY() const override { return 0.0f; }
        [[nodiscard]] rc::f32 ScrollX() const override { return 0.0f; }
        [[nodiscard]] rc::f32 ScrollY() const override { return 0.0f; }
        [[nodiscard]] bool IsButtonDown(MouseButton) const override { return false; }
        [[nodiscard]] bool IsButtonPressed(MouseButton) const override { return false; }
        [[nodiscard]] bool IsButtonReleased(MouseButton) const override { return false; }
        [[nodiscard]] bool RelativeMode() const override { return false; }
        void SetRelativeMode(bool) override {}
        [[nodiscard]] bool CursorVisible() const override { return true; }
        void SetCursorVisible(bool) override {}
        void SetCursor(CursorType) override {}
    };

    class NullTouch final : public ITouch
    {
    public:
        [[nodiscard]] rc::i32 TouchCount() const override { return 0; }
        [[nodiscard]] bool GetTouchPoint(rc::i32, TouchPoint&) const override { return false; }
        [[nodiscard]] bool HasTouch() const override { return false; }
    };

    class NullInputManager final : public IInputManager
    {
    public:
        [[nodiscard]] IKeyboard* Keyboard() override { return &m_keyboard; }
        [[nodiscard]] IMouse*    Mouse()    override { return &m_mouse; }
        [[nodiscard]] ITouch*    Touch()    override { return &m_touch; }
        [[nodiscard]] rc::i32    GamepadCount() const override { return 0; }
        [[nodiscard]] IGamepad*  GetGamepad(rc::i32) override { return nullptr; }
        void Update() override {}

    private:
        NullKeyboard m_keyboard;
        NullMouse    m_mouse;
        NullTouch    m_touch;
    };

    class NullPlatform final : public IPlatform
    {
    public:
        explicit NullPlatform(const WindowSettings& settings = {}) noexcept : m_window(settings) {}

        [[nodiscard]] IWindow* MainWindow() noexcept override { return &m_window; }
        [[nodiscard]] IInputManager* Input() noexcept override { return &m_input; }
        void ProcessEvents() override {}  // no OS event source
        [[nodiscard]] bool IsRunning() const noexcept override { return m_running && m_window.IsOpen(); }
        void RequestExit() override { m_running = false; }

    private:
        NullWindow m_window;
        NullInputManager m_input;
        bool m_running = true;
    };
}
