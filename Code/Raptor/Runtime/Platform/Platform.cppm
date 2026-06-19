// Raptor::RuntimePlatform — the `raptor.runtime.platform` module.
//
// IPlatform is the raw platform service (Sedulous calls this the "shell"):
// windowing, the OS event pump, run state, and raw input devices (keyboard,
// mouse, gamepad, touch — see the :input / :input_types partitions). It is a
// PASSIVE service, not a subsystem and not the loop owner: the runner drives it
// (ProcessEvents once per frame) and the Application borrows it to wire
// platform-backed subsystems (e.g. a future InputSubsystem). Native backends
// (Win32/Linux/...) implement it; a null backend serves headless and test runs.
// Interfaces only — depends on Core, nothing higher.

module;
#include "Core/Prelude.h"

export module raptor.runtime.platform;

export import :input_types;
export import :input;

import raptor.core;

namespace rc = raptor::core;

export namespace raptor::runtime
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
    // surface creation internally — the platform only hands over the handles).
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
        rc::StringView title = u8"Raptor";
        rc::u32 width = 1280;
        rc::u32 height = 720;
    };

    class IWindow
    {
    public:
        virtual ~IWindow() = default;

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

    class IPlatform
    {
    public:
        virtual ~IPlatform() = default;

        // The main window (may be null for a headless platform).
        [[nodiscard]] virtual IWindow* MainWindow() noexcept = 0;

        // Aggregate input devices (keyboard/mouse/gamepad/touch). Always present
        // — the null backend returns a no-op manager, so callers need not check.
        [[nodiscard]] virtual IInputManager* Input() noexcept = 0;

        // Pump pending OS events once per frame (the runner calls this). The
        // backend rolls input state (Input()->Update()) before pumping.
        virtual void ProcessEvents() = 0;

        // OS-level run state: false once the platform should quit (e.g. the main
        // window closed). Distinct from Application::IsRunning() (app-level exit).
        [[nodiscard]] virtual bool IsRunning() const noexcept = 0;

        // Ask the platform to quit (flips IsRunning()).
        virtual void RequestExit() = 0;
    };
}
