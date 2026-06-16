// Raptor::RuntimePlatformDesktop — the `raptor.runtime.platform.desktop` module.
//
// The desktop platform target (Windows/Linux/macOS), implemented on SDL3:
// SDL3Platform covers Wayland, X11, Win32, and Cocoa in one backend, plus input,
// clipboard, and Vulkan-surface creation for RHI. SDL is linked dynamically
// (system/prebuilt) and bundled for distribution. We own the entry point
// (SDL_MAIN_HANDLED), so SDL does not hijack main; SDL_SetMainReady() is called
// before SDL_Init.
//
// Other SDL-based targets (Emscripten, Android) get their own modules/folders:
// they share this SDL3 IPlatform shape but differ in run loop (callback vs
// blocking), entry point, and build flags. If the impl ends up duplicated it can
// be extracted into a shared module then.
//
// If SDL video init or window creation fails (e.g. no display), the platform
// degrades: MainWindow() is null and IsRunning() is false, so a runner exits
// immediately rather than crashing.

module;
#include "Core/Prelude.h"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // SDL_SetMainReady (no main hijack with SDL_MAIN_HANDLED)

export module raptor.runtime.platform.desktop;

import raptor.core;
import raptor.runtime.platform;

namespace rc = raptor::core;

export namespace raptor::runtime
{
    class SDL3Window final : public IWindow
    {
    public:
        explicit SDL3Window(SDL_Window* window) noexcept : m_window(window)
        {
            int w = 0, h = 0;
            SDL_GetWindowSize(m_window, &w, &h);
            m_width = static_cast<rc::u32>(w);
            m_height = static_cast<rc::u32>(h);
        }

        ~SDL3Window() override { if (m_window != nullptr) { SDL_DestroyWindow(m_window); } }

        SDL3Window(const SDL3Window&) = delete;
        SDL3Window& operator=(const SDL3Window&) = delete;

        [[nodiscard]] rc::u32 Width() const noexcept override { return m_width; }
        [[nodiscard]] rc::u32 Height() const noexcept override { return m_height; }
        // The SDL_Window* — RHI feeds it to SDL_Vulkan_CreateSurface.
        [[nodiscard]] NativeWindowHandle NativeHandle() const noexcept override { return m_window; }
        [[nodiscard]] bool IsOpen() const noexcept override { return m_open; }
        void Close() override { m_open = false; }

        [[nodiscard]] SDL_Window* Handle() const noexcept { return m_window; }
        void OnResized(rc::u32 w, rc::u32 h) noexcept { m_width = w; m_height = h; }

    private:
        SDL_Window* m_window;
        rc::u32 m_width = 0;
        rc::u32 m_height = 0;
        bool m_open = true;
    };

    class SDL3Platform final : public IPlatform
    {
    public:
        explicit SDL3Platform(const WindowSettings& settings = {}) noexcept
        {
            SDL_SetMainReady();
            if (!SDL_Init(SDL_INIT_VIDEO)) { m_running = false; return; }
            m_initialized = true;

            const rc::UTF8String title = rc::ToUTF8(settings.title);
            SDL_Window* window = SDL_CreateWindow(
                reinterpret_cast<const char*>(title.CStr()),
                static_cast<int>(settings.width), static_cast<int>(settings.height),
                SDL_WINDOW_RESIZABLE);
            if (window == nullptr) { m_running = false; return; }

            m_window = rc::DefaultAllocator().New<SDL3Window>(window);
        }

        ~SDL3Platform() override
        {
            if (m_window != nullptr) { rc::DefaultAllocator().Delete(m_window); }
            if (m_initialized) { SDL_Quit(); }
        }

        SDL3Platform(const SDL3Platform&) = delete;
        SDL3Platform& operator=(const SDL3Platform&) = delete;

        [[nodiscard]] IWindow* MainWindow() noexcept override { return m_window; }

        void ProcessEvents() override
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                switch (event.type)
                {
                    case SDL_EVENT_QUIT:
                    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                        if (m_window != nullptr) { m_window->Close(); }
                        m_running = false;
                        break;
                    case SDL_EVENT_WINDOW_RESIZED:
                        if (m_window != nullptr)
                        {
                            m_window->OnResized(static_cast<rc::u32>(event.window.data1),
                                                static_cast<rc::u32>(event.window.data2));
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        [[nodiscard]] bool IsRunning() const noexcept override
        {
            return m_running && m_window != nullptr && m_window->IsOpen();
        }

        void RequestExit() override { m_running = false; }

    private:
        SDL3Window* m_window = nullptr;
        bool m_initialized = false;
        bool m_running = true;
    };

    // Factory the RAPTOR_APP_MAIN entry point calls to create the platform.
    [[nodiscard]] rc::UniquePtr<IPlatform> CreatePlatform(const WindowSettings& settings = {})
    {
        IPlatform* platform = rc::DefaultAllocator().New<SDL3Platform>(settings);
        return rc::UniquePtr<IPlatform>(platform, rc::DefaultAllocator());
    }
}
