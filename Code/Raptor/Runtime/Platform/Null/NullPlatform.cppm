// Raptor::RuntimePlatformNull — the `raptor.runtime.platform.null` module.
//
// A headless IPlatform implementation: no real window or OS events. Useful for
// tests, tools, and headless servers, and as the reference for what a real
// backend must provide. ProcessEvents is a no-op; IsRunning stays true until
// RequestExit (or the main window is closed), so the runner relies on the
// Application requesting exit to terminate. The window manager is fully
// functional headless (create/destroy/resize) so multi-window logic is testable
// without an OS.

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
        NullWindow(rc::u32 id, const WindowSettings& settings) noexcept
            : m_id(id), m_width(settings.width), m_height(settings.height) {}

        [[nodiscard]] rc::u32 Id() const noexcept override { return m_id; }
        [[nodiscard]] rc::u32 Width() const noexcept override { return m_width; }
        [[nodiscard]] rc::u32 Height() const noexcept override { return m_height; }
        [[nodiscard]] NativeWindow Native() const noexcept override { return {}; }  // headless: no handles
        [[nodiscard]] bool IsOpen() const noexcept override { return m_open; }
        [[nodiscard]] bool IsMinimized() const noexcept override { return m_minimized; }
        void Close() override { m_open = false; }

        // --- test/headless controls (no OS to drive these) ---
        void Resize(rc::u32 w, rc::u32 h) noexcept { m_width = w; m_height = h; }
        void SetMinimized(bool m) noexcept { m_minimized = m; }

    private:
        rc::u32 m_id;
        rc::u32 m_width;
        rc::u32 m_height;
        bool m_open = true;
        bool m_minimized = false;
    };

    class NullWindowManager final : public IWindowManager
    {
    public:
        explicit NullWindowManager(const WindowSettings& main) { (void)CreateWindow(main); }

        [[nodiscard]] rc::Result<IWindow*> CreateWindow(const WindowSettings& settings) override
        {
            const rc::u32 id = m_nextId++;
            auto window = rc::MakeUnique<NullWindow>(rc::DefaultAllocator(), id, settings);
            IWindow* borrowed = window.Get();
            m_owned.PushBack(static_cast<rc::UniquePtr<NullWindow>&&>(window));
            m_live.PushBack(borrowed);
            return borrowed;
        }

        void DestroyWindow(IWindow* window) override
        {
            if (window == nullptr) { return; }
            window->Close();
            m_pendingDestroy.PushBack(window->Id());
        }

        [[nodiscard]] rc::Span<IWindow* const> Windows() noexcept override
        {
            return rc::Span<IWindow* const>(m_live.Data(), m_live.Size());
        }
        [[nodiscard]] IWindow* MainWindow() noexcept override
        {
            return m_live.IsEmpty() ? nullptr : m_live[0];
        }
        [[nodiscard]] IWindow* GetWindow(rc::u32 id) noexcept override
        {
            for (IWindow* w : m_live) { if (w->Id() == id) { return w; } }
            return nullptr;
        }
        [[nodiscard]] rc::Span<const WindowEvent> Events() const noexcept override
        {
            return rc::Span<const WindowEvent>(m_events.Data(), m_events.Size());
        }

        void FlushDestroyed() override
        {
            for (rc::u32 id : m_pendingDestroy)
            {
                for (rc::usize i = 0; i < m_live.Size(); ++i)
                {
                    if (m_live[i]->Id() == id) { m_live.RemoveAt(i); break; }
                }
                for (rc::usize i = 0; i < m_owned.Size(); ++i)
                {
                    if (m_owned[i]->Id() == id) { m_owned.RemoveAt(i); break; }
                }
            }
            m_pendingDestroy.Clear();
        }

    private:
        rc::Array<rc::UniquePtr<NullWindow>> m_owned;
        rc::Array<IWindow*> m_live;          // borrowed parallel pointers for the span
        rc::Array<rc::u32> m_pendingDestroy; // window ids
        rc::Array<WindowEvent> m_events;     // always empty (no OS event source)
        rc::u32 m_nextId = 1;
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
        explicit NullPlatform(const WindowSettings& settings = {}) noexcept : m_windows(settings) {}

        [[nodiscard]] IWindowManager* WindowManager() noexcept override { return &m_windows; }
        [[nodiscard]] IWindow* MainWindow() noexcept override { return m_windows.MainWindow(); }
        [[nodiscard]] IInputManager* Input() noexcept override { return &m_input; }
        void ProcessEvents() override {}  // no OS event source
        [[nodiscard]] bool IsRunning() const noexcept override
        {
            // Running while the main window (if any) is open.
            IWindow* main = const_cast<NullWindowManager&>(m_windows).MainWindow();
            return m_running && (main == nullptr || main->IsOpen());
        }
        void RequestExit() override { m_running = false; }

    private:
        NullWindowManager m_windows;
        NullInputManager m_input;
        bool m_running = true;
    };
}
