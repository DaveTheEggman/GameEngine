// Draconic::RuntimePlatformDesktop — the `draconic.runtime.platform.desktop` module.
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
#include <chrono>
#include <cstdint>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // SDL_SetMainReady (no main hijack with SDL_MAIN_HANDLED)

export module draconic.runtime.platform.desktop;

import draconic.core;
import draconic.runtime.platform;
import draconic.runtime.graphics;  // GraphicsDevice (the runner hands it to the app)
import draconic.runtime.client;    // Application (the desktop runner drives it)

namespace rc = draconic::core;

export namespace draconic::runtime
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
            m_id = static_cast<rc::u32>(SDL_GetWindowID(m_window));
        }

        ~SDL3Window() override { if (m_window != nullptr) { SDL_DestroyWindow(m_window); } }

        SDL3Window(const SDL3Window&) = delete;
        SDL3Window& operator=(const SDL3Window&) = delete;

        [[nodiscard]] rc::u32 Id() const noexcept override { return m_id; }
        [[nodiscard]] rc::u32 Width() const noexcept override { return m_width; }
        [[nodiscard]] rc::u32 Height() const noexcept override { return m_height; }

        // Extract the real native handles from SDL's window properties so RHI can
        // create its own surface (it does not use SDL's Vulkan helpers).
        [[nodiscard]] NativeWindow Native() const noexcept override
        {
            NativeWindow native;
            if (m_window == nullptr) { return native; }
            const SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
#if defined(_WIN32)
            native.system = WindowSystem::Win32;
            native.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
            native.window = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
            native.system = WindowSystem::Cocoa;
            native.window = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#else
            const char* driver = SDL_GetCurrentVideoDriver();
            if (driver != nullptr && SDL_strcmp(driver, "wayland") == 0)
            {
                native.system = WindowSystem::Wayland;
                native.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
                native.window = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
            }
            else if (driver != nullptr && SDL_strcmp(driver, "x11") == 0)
            {
                native.system = WindowSystem::X11;
                native.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
                native.window = reinterpret_cast<void*>(static_cast<std::uintptr_t>(
                    SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
            }
#endif
            return native;
        }

        [[nodiscard]] bool IsOpen() const noexcept override { return m_open; }
        [[nodiscard]] bool IsMinimized() const noexcept override
        {
            return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0;
        }
        void Close() override { m_open = false; }

        [[nodiscard]] SDL_Window* Handle() const noexcept { return m_window; }
        void OnResized(rc::u32 w, rc::u32 h) noexcept { m_width = w; m_height = h; }

    private:
        SDL_Window* m_window;
        rc::u32 m_id = 0;
        rc::u32 m_width = 0;
        rc::u32 m_height = 0;
        bool m_open = true;
    };

    // Builds SDL window-creation flags. On Wayland a Vulkan-backed window is
    // needed for client-side decorations (see SDL3Platform ctor note); skipped
    // under the headless "dummy" driver so tests still get a window.
    [[nodiscard]] inline SDL_WindowFlags Sdl3WindowFlags() noexcept
    {
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
#if defined(__linux__)
        const char* driver = SDL_GetCurrentVideoDriver();
        if (driver != nullptr && SDL_strcmp(driver, "dummy") != 0) { flags |= SDL_WINDOW_VULKAN; }
#endif
        return flags;
    }

    // Owns the SDL windows for the run. The main window is the first created.
    // Window destruction is deferred to FlushDestroyed() so a window is never
    // freed mid-frame while the GPU may still reference its swapchain.
    class SDL3WindowManager final : public IWindowManager
    {
    public:
        [[nodiscard]] rc::Result<IWindow*> CreateWindow(const WindowSettings& settings) override
        {
            const rc::String title = rc::String(settings.title);
            SDL_Window* window = SDL_CreateWindow(
                reinterpret_cast<const char*>(title.CStr()),
                static_cast<int>(settings.width), static_cast<int>(settings.height),
                Sdl3WindowFlags());
            if (window == nullptr) { return rc::Err(rc::ErrorCode::Unknown); }

            auto wrapped = rc::MakeUnique<SDL3Window>(rc::DefaultAllocator(), window);
            IWindow* borrowed = wrapped.Get();
            m_owned.PushBack(static_cast<rc::UniquePtr<SDL3Window>&&>(wrapped));
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
                    if (m_owned[i]->Id() == id) { m_owned.RemoveAt(i); break; }  // dtor destroys SDL window
                }
            }
            m_pendingDestroy.Clear();
        }

        // --- event pump wiring (called by SDL3Platform::ProcessEvents) ---
        SDL3Window* Find(rc::u32 id) noexcept
        {
            for (rc::UniquePtr<SDL3Window>& w : m_owned) { if (w->Id() == id) { return w.Get(); } }
            return nullptr;
        }
        void ClearEvents() noexcept { m_events.Clear(); }
        void PushEvent(const WindowEvent& e) { m_events.PushBack(e); }

        // Destroy every window immediately (SDL3Window dtors call
        // SDL_DestroyWindow). The platform calls this before SDL_Quit().
        void DestroyAllNow()
        {
            m_live.Clear();
            m_owned.Clear();
            m_pendingDestroy.Clear();
        }

    private:
        rc::Array<rc::UniquePtr<SDL3Window>> m_owned;
        rc::Array<IWindow*> m_live;
        rc::Array<rc::u32> m_pendingDestroy;
        rc::Array<WindowEvent> m_events;
    };

    // -----------------------------------------------------------------------
    // Input devices — double-buffered state fed by the SDL3 event pump.
    // -----------------------------------------------------------------------
    inline constexpr rc::u32 kKeyCount           = static_cast<rc::u32>(KeyCode::Count);
    inline constexpr rc::u32 kMouseButtonCount   = static_cast<rc::u32>(MouseButton::Count);
    inline constexpr rc::u32 kGamepadButtonCount = static_cast<rc::u32>(GamepadButton::Count);
    inline constexpr rc::u32 kCursorCount        = static_cast<rc::u32>(CursorType::Count);

    class SDL3Keyboard final : public IKeyboard
    {
    public:
        [[nodiscard]] bool IsKeyDown(KeyCode key) const override { return m_current[Index(key)]; }
        [[nodiscard]] bool IsKeyPressed(KeyCode key) const override
        {
            const rc::u32 i = Index(key);
            return m_current[i] && !m_previous[i];
        }
        [[nodiscard]] bool IsKeyReleased(KeyCode key) const override
        {
            const rc::u32 i = Index(key);
            return !m_current[i] && m_previous[i];
        }
        [[nodiscard]] KeyModifiers Modifiers() const override { return m_mods; }

        void SetKey(KeyCode key, bool down) { m_current[Index(key)] = down; }
        void SetModifiers(KeyModifiers mods) { m_mods = mods; }
        void BeginFrame() { for (rc::u32 i = 0; i < kKeyCount; ++i) { m_previous[i] = m_current[i]; } }

    private:
        static rc::u32 Index(KeyCode key) noexcept
        {
            const rc::u32 i = static_cast<rc::u32>(key);
            return i < kKeyCount ? i : 0;
        }
        bool m_current[kKeyCount] = {};
        bool m_previous[kKeyCount] = {};
        KeyModifiers m_mods = KeyModifiers::None;
    };

    class SDL3Mouse final : public IMouse
    {
    public:
        [[nodiscard]] rc::f32 X() const override { return m_x; }
        [[nodiscard]] rc::f32 Y() const override { return m_y; }
        [[nodiscard]] rc::f32 DeltaX() const override { return m_dx; }
        [[nodiscard]] rc::f32 DeltaY() const override { return m_dy; }
        [[nodiscard]] rc::f32 ScrollX() const override { return m_sx; }
        [[nodiscard]] rc::f32 ScrollY() const override { return m_sy; }
        [[nodiscard]] bool IsButtonDown(MouseButton b) const override { return m_current[Index(b)]; }
        [[nodiscard]] bool IsButtonPressed(MouseButton b) const override
        {
            const rc::u32 i = Index(b);
            return m_current[i] && !m_previous[i];
        }
        [[nodiscard]] bool IsButtonReleased(MouseButton b) const override
        {
            const rc::u32 i = Index(b);
            return !m_current[i] && m_previous[i];
        }
        [[nodiscard]] bool RelativeMode() const override { return m_relative; }
        void SetRelativeMode(bool enabled) override
        {
            if (m_window != nullptr) { SDL_SetWindowRelativeMouseMode(m_window, enabled); }
            m_relative = enabled;
        }
        [[nodiscard]] bool CursorVisible() const override { return m_cursorVisible; }
        void SetCursorVisible(bool visible) override
        {
            if (visible) { SDL_ShowCursor(); } else { SDL_HideCursor(); }
            m_cursorVisible = visible;
        }
        void SetCursor(CursorType cursor) override
        {
            const rc::u32 i = static_cast<rc::u32>(cursor);
            if (i >= kCursorCount) { return; }
            if (m_cursors[i] == nullptr) { m_cursors[i] = SDL_CreateSystemCursor(MapSystemCursor(cursor)); }
            if (m_cursors[i] != nullptr) { SDL_SetCursor(m_cursors[i]); m_cursor = cursor; }
        }

        void SetWindow(SDL_Window* window) { m_window = window; }
        // Frees the lazily-created system cursors. Called before SDL_Quit so no
        // SDL calls happen after the video subsystem is torn down.
        void ReleaseCursors()
        {
            for (SDL_Cursor*& c : m_cursors)
            {
                if (c != nullptr) { SDL_DestroyCursor(c); c = nullptr; }
            }
        }
        void OnMotion(rc::f32 x, rc::f32 y, rc::f32 relX, rc::f32 relY)
        {
            m_x = x; m_y = y; m_dx += relX; m_dy += relY;
        }
        void OnButton(rc::u32 index, bool down) { if (index < kMouseButtonCount) { m_current[index] = down; } }
        void OnWheel(rc::f32 x, rc::f32 y) { m_sx += x; m_sy += y; }
        void BeginFrame()
        {
            for (rc::u32 i = 0; i < kMouseButtonCount; ++i) { m_previous[i] = m_current[i]; }
            m_dx = m_dy = m_sx = m_sy = 0.0f;
        }

    private:
        static rc::u32 Index(MouseButton b) noexcept
        {
            const rc::u32 i = static_cast<rc::u32>(b);
            return i < kMouseButtonCount ? i : 0;
        }

        static SDL_SystemCursor MapSystemCursor(CursorType cursor) noexcept
        {
            switch (cursor)
            {
                case CursorType::Default:    return SDL_SYSTEM_CURSOR_DEFAULT;
                case CursorType::Text:       return SDL_SYSTEM_CURSOR_TEXT;
                case CursorType::Wait:       return SDL_SYSTEM_CURSOR_WAIT;
                case CursorType::Crosshair:  return SDL_SYSTEM_CURSOR_CROSSHAIR;
                case CursorType::Progress:   return SDL_SYSTEM_CURSOR_PROGRESS;
                case CursorType::ResizeNWSE: return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
                case CursorType::ResizeNESW: return SDL_SYSTEM_CURSOR_NESW_RESIZE;
                case CursorType::ResizeEW:   return SDL_SYSTEM_CURSOR_EW_RESIZE;
                case CursorType::ResizeNS:   return SDL_SYSTEM_CURSOR_NS_RESIZE;
                case CursorType::ResizeNW:   return SDL_SYSTEM_CURSOR_NW_RESIZE;
                case CursorType::ResizeN:    return SDL_SYSTEM_CURSOR_N_RESIZE;
                case CursorType::ResizeNE:   return SDL_SYSTEM_CURSOR_NE_RESIZE;
                case CursorType::ResizeE:    return SDL_SYSTEM_CURSOR_E_RESIZE;
                case CursorType::ResizeSE:   return SDL_SYSTEM_CURSOR_SE_RESIZE;
                case CursorType::ResizeS:    return SDL_SYSTEM_CURSOR_S_RESIZE;
                case CursorType::ResizeSW:   return SDL_SYSTEM_CURSOR_SW_RESIZE;
                case CursorType::ResizeW:    return SDL_SYSTEM_CURSOR_W_RESIZE;
                case CursorType::Move:       return SDL_SYSTEM_CURSOR_MOVE;
                case CursorType::NotAllowed: return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
                case CursorType::Pointer:    return SDL_SYSTEM_CURSOR_POINTER;
                default:                     return SDL_SYSTEM_CURSOR_DEFAULT;
            }
        }

        SDL_Window* m_window = nullptr;
        rc::f32 m_x = 0, m_y = 0, m_dx = 0, m_dy = 0, m_sx = 0, m_sy = 0;
        bool m_current[kMouseButtonCount] = {};
        bool m_previous[kMouseButtonCount] = {};
        bool m_relative = false;
        bool m_cursorVisible = true;
        CursorType m_cursor = CursorType::Default;
        SDL_Cursor* m_cursors[kCursorCount] = {};  // lazily created, cached
    };

    class SDL3Gamepad final : public IGamepad
    {
    public:
        SDL3Gamepad(SDL_Gamepad* pad, SDL_JoystickID id, rc::i32 index, rc::String name) noexcept
            : m_pad(pad), m_id(id), m_index(index), m_name(static_cast<rc::String&&>(name)) {}

        [[nodiscard]] rc::i32 Index() const override { return m_index; }
        [[nodiscard]] rc::StringView Name() const override { return m_name; }
        [[nodiscard]] bool Connected() const override { return m_pad != nullptr; }
        [[nodiscard]] bool IsButtonDown(GamepadButton b) const override { return m_current[Index(b)]; }
        [[nodiscard]] bool IsButtonPressed(GamepadButton b) const override
        {
            const rc::u32 i = Index(b);
            return m_current[i] && !m_previous[i];
        }
        [[nodiscard]] bool IsButtonReleased(GamepadButton b) const override
        {
            const rc::u32 i = Index(b);
            return !m_current[i] && m_previous[i];
        }
        [[nodiscard]] rc::f32 Axis(GamepadAxis a) const override
        {
            if (m_pad == nullptr) { return 0.0f; }
            const auto raw = SDL_GetGamepadAxis(m_pad, static_cast<SDL_GamepadAxis>(static_cast<rc::u32>(a)));
            return static_cast<rc::f32>(raw) / 32767.0f;
        }
        void SetRumble(rc::f32 lowFreq, rc::f32 highFreq, rc::u32 durationMs) override
        {
            if (m_pad != nullptr)
            {
                SDL_RumbleGamepad(m_pad,
                                  static_cast<rc::u16>(lowFreq * 65535.0f),
                                  static_cast<rc::u16>(highFreq * 65535.0f),
                                  durationMs);
            }
        }

        [[nodiscard]] SDL_JoystickID Id() const noexcept { return m_id; }
        [[nodiscard]] SDL_Gamepad* Handle() const noexcept { return m_pad; }
        void SetIndex(rc::i32 index) noexcept { m_index = index; }
        void SetButton(GamepadButton b, bool down) { m_current[Index(b)] = down; }
        void Disconnect() noexcept { m_pad = nullptr; }
        void BeginFrame() { for (rc::u32 i = 0; i < kGamepadButtonCount; ++i) { m_previous[i] = m_current[i]; } }

    private:
        static rc::u32 Index(GamepadButton b) noexcept
        {
            const rc::u32 i = static_cast<rc::u32>(b);
            return i < kGamepadButtonCount ? i : 0;
        }
        SDL_Gamepad* m_pad;
        SDL_JoystickID m_id;
        rc::i32 m_index;
        rc::String m_name;
        bool m_current[kGamepadButtonCount] = {};
        bool m_previous[kGamepadButtonCount] = {};
    };

    class SDL3Touch final : public ITouch
    {
    public:
        [[nodiscard]] rc::i32 TouchCount() const override { return static_cast<rc::i32>(m_points.Size()); }
        [[nodiscard]] bool GetTouchPoint(rc::i32 index, TouchPoint& out) const override
        {
            if (index < 0 || static_cast<rc::usize>(index) >= m_points.Size()) { return false; }
            out = m_points[static_cast<rc::usize>(index)];
            return true;
        }
        [[nodiscard]] bool HasTouch() const override { return !m_points.IsEmpty(); }

        void AddOrUpdate(const TouchPoint& tp)
        {
            for (rc::usize i = 0; i < m_points.Size(); ++i)
            {
                if (m_points[i].id == tp.id) { m_points[i] = tp; return; }
            }
            m_points.PushBack(tp);
        }
        void Remove(rc::u64 id)
        {
            for (rc::usize i = 0; i < m_points.Size(); ++i)
            {
                if (m_points[i].id == id) { m_points.RemoveAtSwap(i); return; }
            }
        }

    private:
        rc::Array<TouchPoint> m_points;
    };

    class SDL3InputManager final : public IInputManager
    {
    public:
        ~SDL3InputManager() override { ReleaseDevices(); }

        // Frees all SDL-owned input resources (open gamepads, system cursors).
        // The platform calls this before SDL_Quit; idempotent so the destructor
        // can call it again harmlessly.
        void ReleaseDevices()
        {
            for (SDL3Gamepad* g : m_gamepads)
            {
                if (g->Handle() != nullptr) { SDL_CloseGamepad(g->Handle()); }
                rc::DefaultAllocator().Delete(g);
            }
            m_gamepads.Clear();
            m_mouse.ReleaseCursors();
        }

        [[nodiscard]] IKeyboard* Keyboard() override { return &m_keyboard; }
        [[nodiscard]] IMouse*    Mouse()    override { return &m_mouse; }
        [[nodiscard]] ITouch*    Touch()    override { return &m_touch; }
        [[nodiscard]] rc::i32    GamepadCount() const override { return static_cast<rc::i32>(m_gamepads.Size()); }
        [[nodiscard]] IGamepad*  GetGamepad(rc::i32 index) override
        {
            if (index < 0 || static_cast<rc::usize>(index) >= m_gamepads.Size()) { return nullptr; }
            return m_gamepads[static_cast<rc::usize>(index)];
        }
        void Update() override
        {
            m_keyboard.BeginFrame();
            m_mouse.BeginFrame();
            for (SDL3Gamepad* g : m_gamepads) { g->BeginFrame(); }
        }

        // --- backend wiring (called by the platform event pump) ---
        SDL3Keyboard& Kb() noexcept { return m_keyboard; }
        SDL3Mouse&    Ms() noexcept { return m_mouse; }
        SDL3Touch&    Tc() noexcept { return m_touch; }
        void SetWindow(SDL_Window* window) { m_mouse.SetWindow(window); }

        void AddGamepad(SDL_JoystickID id)
        {
            if (FindById(id) != nullptr) { return; }
            SDL_Gamepad* pad = SDL_OpenGamepad(id);
            if (pad == nullptr) { return; }

            const char* n = SDL_GetGamepadName(pad);
            rc::String name = (n != nullptr)
                ? rc::String(rc::StringView(reinterpret_cast<const rc::utf8char*>(n)))
                : rc::String{};
            const rc::i32 index = static_cast<rc::i32>(m_gamepads.Size());
            m_gamepads.PushBack(rc::DefaultAllocator().New<SDL3Gamepad>(pad, id, index, static_cast<rc::String&&>(name)));
        }

        void RemoveGamepad(SDL_JoystickID id)
        {
            for (rc::usize i = 0; i < m_gamepads.Size(); ++i)
            {
                if (m_gamepads[i]->Id() == id)
                {
                    if (m_gamepads[i]->Handle() != nullptr) { SDL_CloseGamepad(m_gamepads[i]->Handle()); }
                    rc::DefaultAllocator().Delete(m_gamepads[i]);
                    m_gamepads.RemoveAt(i);
                    for (rc::usize j = 0; j < m_gamepads.Size(); ++j) { m_gamepads[j]->SetIndex(static_cast<rc::i32>(j)); }
                    return;
                }
            }
        }

        SDL3Gamepad* FindById(SDL_JoystickID id)
        {
            for (SDL3Gamepad* g : m_gamepads) { if (g->Id() == id) { return g; } }
            return nullptr;
        }

    private:
        SDL3Keyboard m_keyboard;
        SDL3Mouse    m_mouse;
        SDL3Touch    m_touch;
        rc::Array<SDL3Gamepad*> m_gamepads;
    };

    class SDL3Platform final : public IPlatform
    {
    public:
        // Note on the Wayland Vulkan-window quirk: SDL only attaches libdecor
        // client-side decorations to a window backed by a GPU surface, so a plain
        // window comes up bare on GNOME/Mutter. Sdl3WindowFlags() flags every
        // window as Vulkan on Linux (skipped under the "dummy" driver) to fix it.
        explicit SDL3Platform(const WindowSettings& settings = {}) noexcept
        {
            SDL_SetMainReady();
            if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { m_running = false; return; }
            m_initialized = true;

            rc::Result<IWindow*> main = m_windows.CreateWindow(settings);
            if (!main.HasValue()) { m_running = false; return; }
            if (SDL3Window* w = m_windows.Find(main.Value()->Id())) { m_input.SetWindow(w->Handle()); }
        }

        ~SDL3Platform() override
        {
            // Release SDL-owned input resources and destroy windows before
            // tearing SDL down (no SDL calls may happen after SDL_Quit).
            m_input.ReleaseDevices();
            m_windows.DestroyAllNow();
            if (m_initialized) { SDL_Quit(); }
        }

        SDL3Platform(const SDL3Platform&) = delete;
        SDL3Platform& operator=(const SDL3Platform&) = delete;

        [[nodiscard]] IWindowManager* WindowManager() noexcept override { return &m_windows; }
        [[nodiscard]] IWindow* MainWindow() noexcept override { return m_windows.MainWindow(); }
        [[nodiscard]] IInputManager* Input() noexcept override { return &m_input; }

        void ProcessEvents() override
        {
            // Roll input state (current -> previous, clear deltas) before pumping;
            // clear last frame's window events (they're valid only until now).
            m_input.Update();
            m_windows.ClearEvents();

            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                switch (event.type)
                {
                    case SDL_EVENT_QUIT:
                        // App-level quit: close the main window and stop the loop.
                        if (IWindow* main = m_windows.MainWindow())
                        {
                            main->Close();
                            m_windows.PushEvent(WindowEvent{ WindowEventType::CloseRequested, main->Id() });
                        }
                        m_running = false;
                        break;
                    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    {
                        const rc::u32 id = static_cast<rc::u32>(event.window.windowID);
                        m_windows.PushEvent(WindowEvent{ WindowEventType::CloseRequested, id });
                        // Closing the main window stops the platform; the
                        // Application handles secondary-window close via the event.
                        IWindow* main = m_windows.MainWindow();
                        if (main != nullptr && main->Id() == id) { main->Close(); m_running = false; }
                        break;
                    }
                    case SDL_EVENT_WINDOW_RESIZED:
                    {
                        const rc::u32 id = static_cast<rc::u32>(event.window.windowID);
                        if (SDL3Window* w = m_windows.Find(id))
                        {
                            const rc::u32 nw = static_cast<rc::u32>(event.window.data1);
                            const rc::u32 nh = static_cast<rc::u32>(event.window.data2);
                            w->OnResized(nw, nh);
                            m_windows.PushEvent(WindowEvent{ WindowEventType::Resized, id, nw, nh });
                        }
                        break;
                    }
                    case SDL_EVENT_WINDOW_FOCUS_GAINED:
                        m_windows.PushEvent(WindowEvent{ WindowEventType::FocusGained,
                            static_cast<rc::u32>(event.window.windowID) });
                        break;
                    case SDL_EVENT_WINDOW_FOCUS_LOST:
                        m_windows.PushEvent(WindowEvent{ WindowEventType::FocusLost,
                            static_cast<rc::u32>(event.window.windowID) });
                        break;

                    // --- Keyboard ---
                    case SDL_EVENT_KEY_DOWN:
                    case SDL_EVENT_KEY_UP:
                        m_input.Kb().SetKey(MapKeyCode(event.key.scancode), event.key.down);
                        m_input.Kb().SetModifiers(MapModifiers(event.key.mod));
                        break;

                    // --- Mouse ---
                    case SDL_EVENT_MOUSE_MOTION:
                        m_input.Ms().OnMotion(event.motion.x, event.motion.y,
                                              event.motion.xrel, event.motion.yrel);
                        break;
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    case SDL_EVENT_MOUSE_BUTTON_UP:
                        m_input.Ms().OnButton(static_cast<rc::u32>(event.button.button) - 1,
                                              event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                        break;
                    case SDL_EVENT_MOUSE_WHEEL:
                        m_input.Ms().OnWheel(event.wheel.x, event.wheel.y);
                        break;

                    // --- Touch ---
                    case SDL_EVENT_FINGER_DOWN:
                    case SDL_EVENT_FINGER_MOTION:
                        m_input.Tc().AddOrUpdate(TouchPoint{
                            static_cast<rc::u64>(event.tfinger.fingerID),
                            event.tfinger.x, event.tfinger.y, event.tfinger.pressure });
                        break;
                    case SDL_EVENT_FINGER_UP:
                        m_input.Tc().Remove(static_cast<rc::u64>(event.tfinger.fingerID));
                        break;

                    // --- Gamepad ---
                    case SDL_EVENT_GAMEPAD_ADDED:
                        m_input.AddGamepad(event.gdevice.which);
                        break;
                    case SDL_EVENT_GAMEPAD_REMOVED:
                        m_input.RemoveGamepad(event.gdevice.which);
                        break;
                    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    case SDL_EVENT_GAMEPAD_BUTTON_UP:
                        if (SDL3Gamepad* pad = m_input.FindById(event.gbutton.which))
                        {
                            const GamepadButton b = MapGamepadButton(static_cast<SDL_GamepadButton>(event.gbutton.button));
                            if (b != GamepadButton::Count)
                            {
                                pad->SetButton(b, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
                            }
                        }
                        break;

                    default:
                        break;
                }
            }
        }

        [[nodiscard]] bool IsRunning() const noexcept override
        {
            IWindow* main = const_cast<SDL3WindowManager&>(m_windows).MainWindow();
            return m_running && main != nullptr && main->IsOpen();
        }

        void RequestExit() override { m_running = false; }

    private:
        static KeyCode MapKeyCode(SDL_Scancode sc) noexcept
        {
            if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
                return static_cast<KeyCode>(static_cast<rc::u32>(KeyCode::A) + (sc - SDL_SCANCODE_A));
            if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
                return static_cast<KeyCode>(static_cast<rc::u32>(KeyCode::Num1) + (sc - SDL_SCANCODE_1));
            if (sc == SDL_SCANCODE_0) return KeyCode::Num0;
            if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)
                return static_cast<KeyCode>(static_cast<rc::u32>(KeyCode::F1) + (sc - SDL_SCANCODE_F1));
            switch (sc)
            {
                case SDL_SCANCODE_RETURN:    return KeyCode::Return;
                case SDL_SCANCODE_ESCAPE:    return KeyCode::Escape;
                case SDL_SCANCODE_BACKSPACE: return KeyCode::Backspace;
                case SDL_SCANCODE_TAB:       return KeyCode::Tab;
                case SDL_SCANCODE_SPACE:     return KeyCode::Space;
                case SDL_SCANCODE_UP:        return KeyCode::Up;
                case SDL_SCANCODE_DOWN:      return KeyCode::Down;
                case SDL_SCANCODE_LEFT:      return KeyCode::Left;
                case SDL_SCANCODE_RIGHT:     return KeyCode::Right;
                case SDL_SCANCODE_LCTRL:     return KeyCode::LeftCtrl;
                case SDL_SCANCODE_LSHIFT:    return KeyCode::LeftShift;
                case SDL_SCANCODE_LALT:      return KeyCode::LeftAlt;
                case SDL_SCANCODE_LGUI:      return KeyCode::LeftGui;
                case SDL_SCANCODE_RCTRL:     return KeyCode::RightCtrl;
                case SDL_SCANCODE_RSHIFT:    return KeyCode::RightShift;
                case SDL_SCANCODE_RALT:      return KeyCode::RightAlt;
                case SDL_SCANCODE_RGUI:      return KeyCode::RightGui;
                case SDL_SCANCODE_DELETE:    return KeyCode::Delete;
                case SDL_SCANCODE_INSERT:    return KeyCode::Insert;
                case SDL_SCANCODE_HOME:      return KeyCode::Home;
                case SDL_SCANCODE_END:       return KeyCode::End;
                case SDL_SCANCODE_PAGEUP:    return KeyCode::PageUp;
                case SDL_SCANCODE_PAGEDOWN:  return KeyCode::PageDown;
                default:                     return KeyCode::Unknown;
            }
        }

        static KeyModifiers MapModifiers(SDL_Keymod mod) noexcept
        {
            KeyModifiers m = KeyModifiers::None;
            if (mod & SDL_KMOD_LSHIFT) { m |= KeyModifiers::LeftShift; }
            if (mod & SDL_KMOD_RSHIFT) { m |= KeyModifiers::RightShift; }
            if (mod & SDL_KMOD_LCTRL)  { m |= KeyModifiers::LeftCtrl; }
            if (mod & SDL_KMOD_RCTRL)  { m |= KeyModifiers::RightCtrl; }
            if (mod & SDL_KMOD_LALT)   { m |= KeyModifiers::LeftAlt; }
            if (mod & SDL_KMOD_RALT)   { m |= KeyModifiers::RightAlt; }
            if (mod & SDL_KMOD_LGUI)   { m |= KeyModifiers::LeftGui; }
            if (mod & SDL_KMOD_RGUI)   { m |= KeyModifiers::RightGui; }
            if (mod & SDL_KMOD_NUM)    { m |= KeyModifiers::NumLock; }
            if (mod & SDL_KMOD_CAPS)   { m |= KeyModifiers::CapsLock; }
            if (mod & SDL_KMOD_SCROLL) { m |= KeyModifiers::ScrollLock; }
            return m;
        }

        // SDL's button order differs from ours, so map explicitly.
        static GamepadButton MapGamepadButton(SDL_GamepadButton b) noexcept
        {
            switch (b)
            {
                case SDL_GAMEPAD_BUTTON_SOUTH:          return GamepadButton::South;
                case SDL_GAMEPAD_BUTTON_EAST:           return GamepadButton::East;
                case SDL_GAMEPAD_BUTTON_WEST:           return GamepadButton::West;
                case SDL_GAMEPAD_BUTTON_NORTH:          return GamepadButton::North;
                case SDL_GAMEPAD_BUTTON_BACK:           return GamepadButton::Back;
                case SDL_GAMEPAD_BUTTON_GUIDE:          return GamepadButton::Guide;
                case SDL_GAMEPAD_BUTTON_START:          return GamepadButton::Start;
                case SDL_GAMEPAD_BUTTON_LEFT_STICK:     return GamepadButton::LeftStick;
                case SDL_GAMEPAD_BUTTON_RIGHT_STICK:    return GamepadButton::RightStick;
                case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  return GamepadButton::LeftShoulder;
                case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return GamepadButton::RightShoulder;
                case SDL_GAMEPAD_BUTTON_DPAD_UP:        return GamepadButton::DPadUp;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:      return GamepadButton::DPadDown;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:      return GamepadButton::DPadLeft;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:     return GamepadButton::DPadRight;
                case SDL_GAMEPAD_BUTTON_MISC1:          return GamepadButton::Misc1;
                case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:   return GamepadButton::LeftPaddle1;
                case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:   return GamepadButton::LeftPaddle2;
                case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:  return GamepadButton::RightPaddle1;
                case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:  return GamepadButton::RightPaddle2;
                case SDL_GAMEPAD_BUTTON_TOUCHPAD:       return GamepadButton::Touchpad;
                default:                                return GamepadButton::Count;  // unmapped
            }
        }

        SDL3WindowManager m_windows;
        SDL3InputManager m_input;
        bool m_initialized = false;
        bool m_running = true;
    };

    // Factory the DRACONIC_APP_MAIN entry point calls to create the platform.
    [[nodiscard]] rc::UniquePtr<IPlatform> CreatePlatform(const WindowSettings& settings = {})
    {
        IPlatform* platform = rc::DefaultAllocator().New<SDL3Platform>(settings);
        return rc::UniquePtr<IPlatform>(platform, rc::DefaultAllocator());
    }

    // The desktop runner: a blocking wall-clock loop driving the Application
    // against the platform, clamped to maxFrameTime. This lives here (not in the
    // client) because owning the loop is execution-model-specific — desktop
    // blocks, Emscripten uses a callback — and it must know both Application and
    // IPlatform. DRACONIC_APP_MAIN calls it on desktop. Returns the exit code.
    inline int RunApplication(IApplication& app, IPlatform& platform, GraphicsDevice* graphics = nullptr)
    {
        ApplicationHost host;
        host.Start(app, &platform, graphics);
        auto previous = std::chrono::steady_clock::now();
        while (platform.IsRunning() && host.IsRunning())
        {
            platform.ProcessEvents();
            const auto now = std::chrono::steady_clock::now();
            rc::f32 dt = std::chrono::duration<rc::f32>(now - previous).count();
            previous = now;
            if (dt > host.Settings().maxFrameTime) { dt = host.Settings().maxFrameTime; }
            host.Tick(dt);
        }
        host.Stop();
        return host.ExitCode();
    }
}
