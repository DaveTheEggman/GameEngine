// Raptor::RuntimePlatform — the `raptor.runtime.platform` module.
//
// IPlatform is the raw platform service (Sedulous calls this the "shell"):
// windowing, the OS event pump, run state, and — later — raw input devices. It
// is a PASSIVE service, not a subsystem and not the loop owner: the runner
// drives it (ProcessEvents once per frame) and the Application borrows it to
// wire platform-backed subsystems (e.g. a future InputSubsystem). Native
// backends (Win32/Linux/...) implement it; a null backend serves headless and
// test runs. Interfaces only — depends on Core, nothing higher.

module;
#include "Core/Prelude.h"

export module raptor.runtime.platform;

import raptor.core;

namespace rc = raptor::core;

export namespace raptor::runtime
{
    // Opaque native window handle (HWND, xcb_window_t, canvas id, ...). RHI casts
    // it back to the concrete handle for swapchain creation.
    using NativeWindowHandle = void*;

    struct WindowSettings
    {
        rc::StringView title = u"Raptor";
        rc::u32 width = 1280;
        rc::u32 height = 720;
    };

    class IWindow
    {
    public:
        virtual ~IWindow() = default;

        [[nodiscard]] virtual rc::u32 Width() const noexcept = 0;
        [[nodiscard]] virtual rc::u32 Height() const noexcept = 0;
        [[nodiscard]] virtual NativeWindowHandle NativeHandle() const noexcept = 0;
        [[nodiscard]] virtual bool IsOpen() const noexcept = 0;
        virtual void Close() = 0;
    };

    class IPlatform
    {
    public:
        virtual ~IPlatform() = default;

        // The main window (may be null for a headless platform).
        [[nodiscard]] virtual IWindow* MainWindow() noexcept = 0;

        // Pump pending OS events once per frame (the runner calls this).
        virtual void ProcessEvents() = 0;

        // OS-level run state: false once the platform should quit (e.g. the main
        // window closed). Distinct from Application::IsRunning() (app-level exit).
        [[nodiscard]] virtual bool IsRunning() const noexcept = 0;

        // Ask the platform to quit (flips IsRunning()).
        virtual void RequestExit() = 0;
    };
}
