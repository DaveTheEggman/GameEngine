// Draconic::ShellNull — the `draconic.shell.null` module.
//
// A headless IShell implementation: no real window or OS events. Useful for
// tests, tools, and headless servers, and as the reference for what a real
// backend must provide. ProcessEvents is a no-op; IsRunning stays true until
// RequestExit (or the main window is closed), so the runner relies on the
// Application requesting exit to terminate. The window manager is fully
// functional headless (create/destroy/resize) so multi-window logic is testable
// without an OS.

module;
#include "Core/Prelude.h"

export module draconic.shell.null;

import draconic.core;
import draconic.shell;

namespace core = draconic::core;

export namespace draconic::shell
{
    class NullWindow final : public IWindow
    {
    public:
        NullWindow(core::u32 id, const WindowSettings& settings) noexcept
            : m_id(id), m_width(settings.width), m_height(settings.height) {}

        [[nodiscard]] core::u32 Id() const noexcept override { return m_id; }
        [[nodiscard]] core::u32 Width() const noexcept override { return m_width; }
        [[nodiscard]] core::u32 Height() const noexcept override { return m_height; }
        [[nodiscard]] NativeWindow Native() const noexcept override { return {}; }  // headless: no handles
        [[nodiscard]] bool IsOpen() const noexcept override { return m_open; }
        [[nodiscard]] bool IsMinimized() const noexcept override { return m_minimized; }
        void Close() override { m_open = false; }

        // --- test/headless controls (no OS to drive these) ---
        void Resize(core::u32 w, core::u32 h) noexcept { m_width = w; m_height = h; }
        void SetMinimized(bool m) noexcept { m_minimized = m; }

    private:
        core::u32 m_id;
        core::u32 m_width;
        core::u32 m_height;
        bool m_open = true;
        bool m_minimized = false;
    };

    class NullWindowManager final : public IWindowManager
    {
    public:
        explicit NullWindowManager(const WindowSettings& main) { (void)CreateWindow(main); }

        [[nodiscard]] core::Result<IWindow*> CreateWindow(const WindowSettings& settings) override
        {
            const core::u32 id = m_nextId++;
            auto window = core::MakeUnique<NullWindow>(core::DefaultAllocator(), id, settings);
            IWindow* borrowed = window.Get();
            m_owned.PushBack(static_cast<core::UniquePtr<NullWindow>&&>(window));
            m_live.PushBack(borrowed);
            if (m_mainWindowId == 0) { m_mainWindowId = id; }   // the first window created is the main window
            return borrowed;
        }

        void DestroyWindow(IWindow* window) override
        {
            if (!Owns(window)) { return; }   // no-op for null or windows this manager does not own
            window->Close();
            m_pendingDestroy.PushBack(window->Id());
        }

        [[nodiscard]] core::Span<IWindow* const> Windows() noexcept override
        {
            return core::Span<IWindow* const>(m_live.Data(), m_live.Size());
        }
        [[nodiscard]] IWindow* MainWindow() noexcept override
        {
            // Tracked by id, so destroying/flushing the main window never promotes another
            // window into its place; returns null once the main window is gone.
            return GetWindow(m_mainWindowId);
        }
        [[nodiscard]] IWindow* GetWindow(core::u32 id) noexcept override
        {
            for (IWindow* w : m_live) { if (w->Id() == id) { return w; } }
            return nullptr;
        }
        [[nodiscard]] core::Span<const WindowEvent> Events() const noexcept override
        {
            return core::Span<const WindowEvent>(m_events.Data(), m_events.Size());
        }

        void FlushDestroyed() override
        {
            for (core::u32 id : m_pendingDestroy)
            {
                for (core::usize i = 0; i < m_live.Size(); ++i)
                {
                    if (m_live[i]->Id() == id) { m_live.RemoveAt(i); break; }
                }
                for (core::usize i = 0; i < m_owned.Size(); ++i)
                {
                    if (m_owned[i]->Id() == id) { m_owned.RemoveAt(i); break; }
                }
            }
            m_pendingDestroy.Clear();
        }

    private:
        // True only for windows this manager owns (present in m_live). Rejects nullptr too, so
        // DestroyWindow() is a no-op for null/unknown windows. Checked by pointer identity, NOT id:
        // a window from another manager can share an id, and acting on it would corrupt bookkeeping.
        [[nodiscard]] bool Owns(IWindow* window) const noexcept
        {
            for (IWindow* w : m_live) { if (w == window) { return true; } }
            return false;
        }

        core::Array<core::UniquePtr<NullWindow>> m_owned;
        core::Array<IWindow*> m_live;          // borrowed parallel pointers for the span
        core::Array<core::u32> m_pendingDestroy; // window ids
        core::Array<WindowEvent> m_events;     // always empty (no OS event source)
        core::u32 m_nextId = 1;
        core::u32 m_mainWindowId = 0;          // id of the main window (first created); 0 = none
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
        [[nodiscard]] core::f32 X() const override { return 0.0f; }
        [[nodiscard]] core::f32 Y() const override { return 0.0f; }
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
    };

    class NullTouch final : public ITouch
    {
    public:
        [[nodiscard]] core::i32 TouchCount() const override { return 0; }
        [[nodiscard]] bool GetTouchPoint(core::i32, TouchPoint&) const override { return false; }
        [[nodiscard]] bool HasTouch() const override { return false; }
    };

    class NullInputManager final : public IInputManager
    {
    public:
        [[nodiscard]] IKeyboard* Keyboard() override { return &m_keyboard; }
        [[nodiscard]] IMouse*    Mouse()    override { return &m_mouse; }
        [[nodiscard]] ITouch*    Touch()    override { return &m_touch; }
        [[nodiscard]] core::i32    GamepadCount() const override { return 0; }
        [[nodiscard]] IGamepad*  GetGamepad(core::i32) override { return nullptr; }
        [[nodiscard]] core::Span<const InputEvent> Events() const override { return {}; }
        [[nodiscard]] core::u32    HoverWindow()   const override { return 0; }
        [[nodiscard]] core::u32    FocusedWindow() const override { return 0; }
        void Update() override {}

    private:
        NullKeyboard m_keyboard;
        NullMouse    m_mouse;
        NullTouch    m_touch;
    };

    class NullShell final : public IShell
    {
    public:
        explicit NullShell(const WindowSettings& settings = {}) noexcept : m_windows(settings) {}

        [[nodiscard]] IWindowManager* WindowManager() noexcept override { return &m_windows; }
        [[nodiscard]] IWindow* MainWindow() noexcept override { return m_windows.MainWindow(); }
        [[nodiscard]] IInputManager* Input() noexcept override { return &m_input; }
        void ProcessEvents() override {}  // no OS event source
        [[nodiscard]] bool IsRunning() const noexcept override
        {
            // Running until RequestExit() or the main window is closed/destroyed.
            IWindow* main = const_cast<NullWindowManager&>(m_windows).MainWindow();
            return m_running && main != nullptr && main->IsOpen();
        }
        void RequestExit() override { m_running = false; }

    private:
        NullWindowManager m_windows;
        NullInputManager m_input;
        bool m_running = true;
    };
}
