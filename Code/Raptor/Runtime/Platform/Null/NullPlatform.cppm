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
        void Close() override { m_open = false; }

    private:
        rc::u32 m_width;
        rc::u32 m_height;
        bool m_open = true;
    };

    class NullPlatform final : public IPlatform
    {
    public:
        explicit NullPlatform(const WindowSettings& settings = {}) noexcept : m_window(settings) {}

        [[nodiscard]] IWindow* MainWindow() noexcept override { return &m_window; }
        void ProcessEvents() override {}  // no OS event source
        [[nodiscard]] bool IsRunning() const noexcept override { return m_running && m_window.IsOpen(); }
        void RequestExit() override { m_running = false; }

    private:
        NullWindow m_window;
        bool m_running = true;
    };
}
