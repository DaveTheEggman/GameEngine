// Draconic::Shell — the `draconic.shell` module.
//
// IShell is the raw OS/window service — the "shell" (Sedulous's term for it):
// windowing, the OS event pump, run state, and raw input devices (keyboard,
// mouse, gamepad, touch — see the :input / :input_types partitions). It is a
// PASSIVE service, not a subsystem and not the loop owner: the runner drives it
// (ProcessEvents once per frame) and the Application borrows it to wire
// shell-backed subsystems (e.g. a future InputSubsystem). Native backends
// (Win32/Linux/...) implement it; a null backend serves headless and test runs.
// Interfaces only — depends on Core, nothing higher.

module;
#include "Core/Prelude.h"

export module draconic.shell;

export import :input_types;
export import :input;
export import :surface;

import draconic.core;

namespace rc = draconic::core;

export namespace draconic::shell
{
    enum class WindowSystem : rc::u8
    {
        Unknown,
        Win32,
        X11,
        Wayland,
        Cocoa,
    };

    // The native handles RHI needs to create a surface/swapchain itself (RHI does
    // surface creation internally — the shell only hands over the handles).
    // Interpretation depends on `system`:
    //   Win32   — display = HINSTANCE,   window = HWND
    //   X11     — display = Display*,     window = Window (XID, via uintptr)
    //   Wayland — display = wl_display*,  window = wl_surface*
    //   Cocoa   — display = nullptr,      window = NSWindow*
    struct NativeWindow
    {
        WindowSystem system = WindowSystem::Unknown;
        void* display = nullptr;
        void* window = nullptr;
    };

    struct WindowSettings
    {
        rc::StringView title = u8"Draconic";
        rc::u32 width = 1280;
        rc::u32 height = 720;
    };

    class IWindow
    {
    public:
        virtual ~IWindow() = default;

        // Stable per-window id, unique within a shell run. Used to route OS
        // events to the right window and to look windows up. 0 is never a valid id.
        [[nodiscard]] virtual rc::u32 Id() const noexcept = 0;

        [[nodiscard]] virtual rc::u32 Width() const noexcept = 0;
        [[nodiscard]] virtual rc::u32 Height() const noexcept = 0;
        // Native handles for RHI surface creation (see NativeWindow).
        [[nodiscard]] virtual NativeWindow Native() const noexcept = 0;
        [[nodiscard]] virtual bool IsOpen() const noexcept = 0;
        // True while the window is minimized (renderers skip drawing). Resize is
        // detected by polling Width()/Height().
        [[nodiscard]] virtual bool IsMinimized() const noexcept = 0;
        virtual void Close() = 0;
    };

    // What happened to a window during the last ProcessEvents() pump. Delivered
    // as a per-frame queue (IWindowManager::Events) rather than a callback —
    // matches the pull-based event model and sidesteps callback lifetime in a
    // -fno-exceptions/-fno-rtti world. The consumer (Application) drains it each
    // frame and reacts (resize that window's swapchain, close it, etc.).
    enum class WindowEventType : rc::u8
    {
        Resized,
        Moved,
        FocusGained,
        FocusLost,
        CloseRequested,
    };

    struct WindowEvent
    {
        WindowEventType type = WindowEventType::Resized;
        rc::u32 windowId = 0;
        rc::u32 width = 0;   // Resized
        rc::u32 height = 0;  // Resized
        rc::i32 x = 0;       // Moved
        rc::i32 y = 0;       // Moved
    };

    // Owns the set of OS windows for a shell run. One manager per shell;
    // the main window is just the first one created. Windows can be created and
    // destroyed at runtime (the basis for detachable/dockable UI windows).
    // Destruction is DEFERRED: DestroyWindow() marks a window closed, and
    // FlushDestroyed() (called at frame end, after the GPU is done with it)
    // actually frees it — so a window is never torn down mid-frame.
    class IWindowManager
    {
    public:
        virtual ~IWindowManager() = default;

        [[nodiscard]] virtual rc::Result<IWindow*> CreateWindow(const WindowSettings& settings) = 0;
        // Mark a window for destruction at the next FlushDestroyed(). Safe to call
        // mid-frame. No-op if the window is unknown.
        virtual void DestroyWindow(IWindow* window) = 0;

        // All currently-live windows (closed-but-not-yet-flushed ones included
        // until FlushDestroyed runs). The main window is Windows()[0] while open.
        [[nodiscard]] virtual rc::Span<IWindow* const> Windows() noexcept = 0;
        [[nodiscard]] virtual IWindow* MainWindow() noexcept = 0;       // first window, or null
        [[nodiscard]] virtual IWindow* GetWindow(rc::u32 id) noexcept = 0;

        // Window events accumulated during the last ProcessEvents() pump. Valid
        // until the next pump. Drained by the runner/Application each frame.
        [[nodiscard]] virtual rc::Span<const WindowEvent> Events() const noexcept = 0;

        // Free windows marked by DestroyWindow(). Call once per frame, at end,
        // after the GPU has finished the frame that may have used them.
        virtual void FlushDestroyed() = 0;
    };

    class IShell
    {
    public:
        virtual ~IShell() = default;

        // The window manager (always present; owns 0..N windows). The main
        // window is WindowManager()->MainWindow().
        [[nodiscard]] virtual IWindowManager* WindowManager() noexcept = 0;

        // Convenience for single-window callers: == WindowManager()->MainWindow().
        // (Kept so single-window hosts like the RHI sample framework are unchanged.)
        [[nodiscard]] virtual IWindow* MainWindow() noexcept = 0;

        // Aggregate input devices (keyboard/mouse/gamepad/touch). Always present
        // — the null backend returns a no-op manager, so callers need not check.
        [[nodiscard]] virtual IInputManager* Input() noexcept = 0;

        // Pump pending OS events once per frame (the runner calls this). The
        // backend rolls input state (Input()->Update()) before pumping.
        virtual void ProcessEvents() = 0;

        // OS-level run state: false once the shell should quit (e.g. the main
        // window closed). Distinct from Application::IsRunning() (app-level exit).
        [[nodiscard]] virtual bool IsRunning() const noexcept = 0;

        // Ask the shell to quit (flips IsRunning()).
        virtual void RequestExit() = 0;
    };
}
