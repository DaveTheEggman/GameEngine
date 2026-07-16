// Draconic::ShellDesktop - the `draconic.shell.desktop` module.
//
// The desktop shell target (Windows/Linux/macOS), implemented on SDL3:
// SDL3Shell covers Wayland, X11, Win32, and Cocoa in one backend, plus input,
// clipboard, and Vulkan-surface creation for RHI. SDL is linked dynamically
// (system/prebuilt) and bundled for distribution. We own the entry point
// (SDL_MAIN_HANDLED), so SDL does not hijack main; SDL_SetMainReady() is called
// before SDL_Init.
//
// Other SDL-based targets (Emscripten, Android) get their own modules/folders:
// they share this SDL3 IShell shape but differ in run loop (callback vs
// blocking), entry point, and build flags. If the impl ends up duplicated it can
// be extracted into a shared module then.
//
// If SDL video init or window creation fails (e.g. no display), the shell
// degrades: MainWindow() is null and IsRunning() is false, so a runner exits
// immediately rather than crashing.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include <cstdint>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // SDL_SetMainReady (no main hijack with SDL_MAIN_HANDLED)

export module draconic.shell.desktop;

import draconic.core;
import draconic.shell;

namespace core = draconic::core;

export namespace draconic::shell
{
    // Human-readable WSI name for the diagnostic log line (compared against the RHI's surface-WSI log).
    [[nodiscard]] inline core::StringView WindowSystemName(WindowSystem s) noexcept
    {
        switch (s)
        {
            case WindowSystem::Win32:   return u8"Win32";
            case WindowSystem::X11:     return u8"X11";
            case WindowSystem::Wayland: return u8"Wayland";
            case WindowSystem::Cocoa:   return u8"Cocoa";
            default:                    return u8"Unknown";
        }
    }

    class SDL3Window final : public IWindow
    {
    public:
        explicit SDL3Window(SDL_Window* window) noexcept : m_window(window)
        {
            int w = 0, h = 0;
            SDL_GetWindowSize(m_window, &w, &h);
            m_width = static_cast<core::u32>(w);
            m_height = static_cast<core::u32>(h);
            m_id = static_cast<core::u32>(SDL_GetWindowID(m_window));
        }

        ~SDL3Window() override { if (m_window != nullptr) { SDL_DestroyWindow(m_window); } }

        SDL3Window(const SDL3Window&) = delete;
        SDL3Window& operator=(const SDL3Window&) = delete;

        [[nodiscard]] core::u32 Id() const noexcept override { return m_id; }
        [[nodiscard]] core::u32 Width() const noexcept override { return m_width; }
        [[nodiscard]] core::u32 Height() const noexcept override { return m_height; }

        [[nodiscard]] core::i32 X() const noexcept override
        {
            int x = 0, y = 0;
            if (m_window != nullptr) { SDL_GetWindowPosition(m_window, &x, &y); }
            return static_cast<core::i32>(x);
        }
        [[nodiscard]] core::i32 Y() const noexcept override
        {
            int x = 0, y = 0;
            if (m_window != nullptr) { SDL_GetWindowPosition(m_window, &x, &y); }
            return static_cast<core::i32>(y);
        }
        void SetPosition(core::i32 x, core::i32 y) override
        {
            if (m_window != nullptr) { SDL_SetWindowPosition(m_window, static_cast<int>(x), static_cast<int>(y)); }
        }
        void SetSize(core::u32 width, core::u32 height) override
        {
            if (m_window != nullptr)
            {
                SDL_SetWindowSize(m_window, static_cast<int>(width), static_cast<int>(height));
                m_width = width;
                m_height = height;
            }
        }
        [[nodiscard]] core::f32 ContentScale() const noexcept override
        {
            const float s = (m_window != nullptr) ? SDL_GetWindowDisplayScale(m_window) : 1.0f;
            return s > 0.0f ? static_cast<core::f32>(s) : 1.0f;   // SDL returns 0 before the window is shown
        }

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

        void StartTextInput() override
        {
            if (m_window != nullptr && !m_textInputActive)
            {
                SDL_StartTextInput(m_window);
                m_textInputActive = true;
            }
        }
        void StopTextInput() override
        {
            if (m_window != nullptr && m_textInputActive)
            {
                SDL_StopTextInput(m_window);
                m_textInputActive = false;
            }
        }
        [[nodiscard]] bool IsTextInputActive() const noexcept override { return m_textInputActive; }

        [[nodiscard]] SDL_Window* Handle() const noexcept { return m_window; }
        void OnResized(core::u32 w, core::u32 h) noexcept { m_width = w; m_height = h; }

    private:
        SDL_Window* m_window;
        core::u32 m_id = 0;
        core::u32 m_width = 0;
        core::u32 m_height = 0;
        bool m_open = true;
        bool m_textInputActive = false;
    };

    // Builds SDL window-creation flags. On Wayland a Vulkan-backed window is
    // needed for client-side decorations (see SDL3Shell ctor note); skipped
    // under the headless "dummy" driver so tests still get a window.
    [[nodiscard]] inline SDL_WindowFlags Sdl3WindowFlags(const WindowSettings& settings) noexcept
    {
        SDL_WindowFlags flags = 0;
        if (settings.resizable) { flags |= SDL_WINDOW_RESIZABLE; }
        if (settings.borderless) { flags |= SDL_WINDOW_BORDERLESS; }
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
        [[nodiscard]] core::Result<IWindow*> CreateWindow(const WindowSettings& settings) override
        {
            const core::String title = core::String(settings.title);
            SDL_Window* window = SDL_CreateWindow(
                reinterpret_cast<const char*>(title.CStr()),
                static_cast<int>(settings.width), static_cast<int>(settings.height),
                Sdl3WindowFlags(settings));
            if (window == nullptr) { return core::Err(core::ErrorCode::Unknown); }

            // Place the window if an explicit position was requested (dockable/floating windows do;
            // the main window leaves it to the OS/centered default).
            if (settings.positioned) { SDL_SetWindowPosition(window, static_cast<int>(settings.x), static_cast<int>(settings.y)); }

            auto wrapped = core::MakeUnique<SDL3Window>(core::DefaultAllocator(), window);
            IWindow* borrowed = wrapped.Get();
            m_owned.PushBack(static_cast<core::UniquePtr<SDL3Window>&&>(wrapped));
            m_live.PushBack(borrowed);
            if (m_mainWindowId == 0) { m_mainWindowId = borrowed->Id(); }   // the first window created is the main window
            return borrowed;
        }

        void DestroyWindow(IWindow* window) override
        {
            if (!Owns(window)) { return; }   // no-op for null or windows this manager does not own
            window->Close();
            m_pendingDestroy.PushBack(window->Id());
        }

        [[nodiscard]] core::Span<IWindow* const> Windows() const noexcept override
        {
            return core::Span<IWindow* const>(m_live.Data(), m_live.Size());
        }
        [[nodiscard]] IWindow* MainWindow() const noexcept override
        {
            // Tracked by id, so destroying/flushing the main window never promotes another
            // window into its place; returns null once the main window is gone.
            return GetWindow(m_mainWindowId);
        }
        [[nodiscard]] IWindow* GetWindow(core::u32 id) const noexcept override
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
                    if (m_owned[i]->Id() == id) { m_owned.RemoveAt(i); break; }  // dtor destroys SDL window
                }
            }
            m_pendingDestroy.Clear();
        }

        // --- event pump wiring (called by SDL3Shell::ProcessEvents) ---
        SDL3Window* Find(core::u32 id) noexcept
        {
            for (core::UniquePtr<SDL3Window>& w : m_owned) { if (w->Id() == id) { return w.Get(); } }
            return nullptr;
        }
        void ClearEvents() noexcept { m_events.Clear(); }
        void PushEvent(const WindowEvent& e) { m_events.PushBack(e); }

        // Destroy every window immediately (SDL3Window dtors call
        // SDL_DestroyWindow). The shell calls this before SDL_Quit().
        void DestroyAllNow()
        {
            m_live.Clear();
            m_owned.Clear();
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

        core::Array<core::UniquePtr<SDL3Window>> m_owned;
        core::Array<IWindow*> m_live;
        core::Array<core::u32> m_pendingDestroy;
        core::Array<WindowEvent> m_events;
        core::u32 m_mainWindowId = 0;   // id of the main window (first created); 0 = none
    };

    // -----------------------------------------------------------------------
    // Input devices - double-buffered state fed by the SDL3 event pump.
    // -----------------------------------------------------------------------
    inline constexpr core::u32 kKeyCount           = static_cast<core::u32>(KeyCode::Count);
    inline constexpr core::u32 kMouseButtonCount   = static_cast<core::u32>(MouseButton::Count);
    inline constexpr core::u32 kGamepadButtonCount = static_cast<core::u32>(GamepadButton::Count);
    inline constexpr core::u32 kCursorCount        = static_cast<core::u32>(CursorType::Count);

    class SDL3Keyboard final : public IKeyboard
    {
    public:
        [[nodiscard]] bool IsKeyDown(KeyCode key) const override { return m_current[Index(key)]; }
        [[nodiscard]] bool IsKeyPressed(KeyCode key) const override
        {
            const core::u32 i = Index(key);
            return m_current[i] && !m_previous[i];
        }
        [[nodiscard]] bool IsKeyReleased(KeyCode key) const override
        {
            const core::u32 i = Index(key);
            return !m_current[i] && m_previous[i];
        }
        [[nodiscard]] KeyModifiers Modifiers() const override { return m_mods; }

        void SetKey(KeyCode key, bool down) { m_current[Index(key)] = down; }
        void SetModifiers(KeyModifiers mods) { m_mods = mods; }
        void BeginFrame() { for (core::u32 i = 0; i < kKeyCount; ++i) { m_previous[i] = m_current[i]; } }

    private:
        static core::u32 Index(KeyCode key) noexcept
        {
            const core::u32 i = static_cast<core::u32>(key);
            return i < kKeyCount ? i : 0;
        }
        bool m_current[kKeyCount] = {};
        bool m_previous[kKeyCount] = {};
        KeyModifiers m_mods = KeyModifiers::None;
    };

    class SDL3Mouse final : public IMouse
    {
    public:
        [[nodiscard]] core::f32 X() const override { return m_x; }
        [[nodiscard]] core::f32 Y() const override { return m_y; }
        [[nodiscard]] core::f32 GlobalX() const override
        {
            float gx = 0.0f, gy = 0.0f;
            SDL_GetGlobalMouseState(&gx, &gy);
            return static_cast<core::f32>(gx);
        }
        [[nodiscard]] core::f32 GlobalY() const override
        {
            float gx = 0.0f, gy = 0.0f;
            SDL_GetGlobalMouseState(&gx, &gy);
            return static_cast<core::f32>(gy);
        }
        [[nodiscard]] core::f32 DeltaX() const override { return m_dx; }
        [[nodiscard]] core::f32 DeltaY() const override { return m_dy; }
        [[nodiscard]] core::f32 ScrollX() const override { return m_sx; }
        [[nodiscard]] core::f32 ScrollY() const override { return m_sy; }
        [[nodiscard]] bool IsButtonDown(MouseButton b) const override { return m_current[Index(b)]; }
        [[nodiscard]] bool IsButtonPressed(MouseButton b) const override
        {
            const core::u32 i = Index(b);
            return m_current[i] && !m_previous[i];
        }
        [[nodiscard]] bool IsButtonReleased(MouseButton b) const override
        {
            const core::u32 i = Index(b);
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
            const core::u32 i = static_cast<core::u32>(cursor);
            if (i >= kCursorCount) { return; }
            if (m_cursors[i] == nullptr) { m_cursors[i] = SDL_CreateSystemCursor(MapSystemCursor(cursor)); }
            if (m_cursors[i] != nullptr) { SDL_SetCursor(m_cursors[i]); m_cursor = cursor; }
        }
        void SetGlobalCapture(bool enabled) override
        {
            if (enabled == m_globalCapture) { return; }   // SDL_CaptureMouse is refcounted-ish; avoid churn
            SDL_CaptureMouse(enabled);
            m_globalCapture = enabled;
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
        void OnMotion(core::f32 x, core::f32 y, core::f32 relX, core::f32 relY)
        {
            m_x = x; m_y = y; m_dx += relX; m_dy += relY;
        }
        void OnButton(core::u32 index, bool down) { if (index < kMouseButtonCount) { m_current[index] = down; } }
        void OnWheel(core::f32 x, core::f32 y) { m_sx += x; m_sy += y; }
        void BeginFrame()
        {
            for (core::u32 i = 0; i < kMouseButtonCount; ++i) { m_previous[i] = m_current[i]; }
            m_dx = m_dy = m_sx = m_sy = 0.0f;
        }

    private:
        static core::u32 Index(MouseButton b) noexcept
        {
            const core::u32 i = static_cast<core::u32>(b);
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
        core::f32 m_x = 0, m_y = 0, m_dx = 0, m_dy = 0, m_sx = 0, m_sy = 0;
        bool m_current[kMouseButtonCount] = {};
        bool m_previous[kMouseButtonCount] = {};
        bool m_relative = false;
        bool m_cursorVisible = true;
        bool m_globalCapture = false;
        CursorType m_cursor = CursorType::Default;
        SDL_Cursor* m_cursors[kCursorCount] = {};  // lazily created, cached
    };

    class SDL3Gamepad final : public IGamepad
    {
    public:
        SDL3Gamepad(SDL_Gamepad* pad, SDL_JoystickID id, core::i32 index, core::String name) noexcept
            : m_pad(pad), m_id(id), m_index(index), m_name(static_cast<core::String&&>(name)) {}
        // Owns the SDL_Gamepad; closing it here means the owning UniquePtr frees the whole device
        // with no manual bookkeeping. Must run before SDL_Quit, which the shell guarantees by
        // clearing the device list in ReleaseDevices().
        ~SDL3Gamepad() override { if (m_pad != nullptr) { SDL_CloseGamepad(m_pad); } }
        SDL3Gamepad(const SDL3Gamepad&) = delete;
        SDL3Gamepad& operator=(const SDL3Gamepad&) = delete;

        [[nodiscard]] core::i32 Index() const override { return m_index; }
        [[nodiscard]] core::StringView Name() const override { return m_name; }
        [[nodiscard]] bool Connected() const override { return m_pad != nullptr; }
        [[nodiscard]] bool IsButtonDown(GamepadButton b) const override { return m_current[Index(b)]; }
        [[nodiscard]] bool IsButtonPressed(GamepadButton b) const override
        {
            const core::u32 i = Index(b);
            return m_current[i] && !m_previous[i];
        }
        [[nodiscard]] bool IsButtonReleased(GamepadButton b) const override
        {
            const core::u32 i = Index(b);
            return !m_current[i] && m_previous[i];
        }
        [[nodiscard]] core::f32 Axis(GamepadAxis a) const override
        {
            if (m_pad == nullptr) { return 0.0f; }
            const auto raw = SDL_GetGamepadAxis(m_pad, static_cast<SDL_GamepadAxis>(static_cast<core::u32>(a)));
            return static_cast<core::f32>(raw) / 32767.0f;
        }
        void SetRumble(core::f32 lowFreq, core::f32 highFreq, core::u32 durationMs) override
        {
            if (m_pad != nullptr)
            {
                SDL_RumbleGamepad(m_pad,
                                  static_cast<core::u16>(lowFreq * 65535.0f),
                                  static_cast<core::u16>(highFreq * 65535.0f),
                                  durationMs);
            }
        }

        [[nodiscard]] SDL_JoystickID Id() const noexcept { return m_id; }
        [[nodiscard]] SDL_Gamepad* Handle() const noexcept { return m_pad; }
        void SetIndex(core::i32 index) noexcept { m_index = index; }
        void SetButton(GamepadButton b, bool down) { m_current[Index(b)] = down; }
        void Disconnect() noexcept { m_pad = nullptr; }
        void BeginFrame() { for (core::u32 i = 0; i < kGamepadButtonCount; ++i) { m_previous[i] = m_current[i]; } }

    private:
        static core::u32 Index(GamepadButton b) noexcept
        {
            const core::u32 i = static_cast<core::u32>(b);
            return i < kGamepadButtonCount ? i : 0;
        }
        SDL_Gamepad* m_pad;
        SDL_JoystickID m_id;
        core::i32 m_index;
        core::String m_name;
        bool m_current[kGamepadButtonCount] = {};
        bool m_previous[kGamepadButtonCount] = {};
    };

    class SDL3Touch final : public ITouch
    {
    public:
        [[nodiscard]] core::i32 TouchCount() const override { return static_cast<core::i32>(m_points.Size()); }
        [[nodiscard]] bool GetTouchPoint(core::i32 index, TouchPoint& out) const override
        {
            if (index < 0 || static_cast<core::usize>(index) >= m_points.Size()) { return false; }
            out = m_points[static_cast<core::usize>(index)];
            return true;
        }
        [[nodiscard]] bool HasTouch() const override { return !m_points.IsEmpty(); }

        void AddOrUpdate(const TouchPoint& tp)
        {
            for (core::usize i = 0; i < m_points.Size(); ++i)
            {
                if (m_points[i].id == tp.id) { m_points[i] = tp; return; }
            }
            m_points.PushBack(tp);
        }
        void Remove(core::u64 id)
        {
            for (core::usize i = 0; i < m_points.Size(); ++i)
            {
                if (m_points[i].id == id) { m_points.RemoveAtSwap(i); return; }
            }
        }

    private:
        core::Array<TouchPoint> m_points;
    };

    class SDL3InputManager final : public IInputManager
    {
    public:
        ~SDL3InputManager() override { ReleaseDevices(); }

        // Frees all SDL-owned input resources (open gamepads, system cursors).
        // The shell calls this before SDL_Quit; idempotent so the destructor
        // can call it again harmlessly.
        void ReleaseDevices()
        {
            m_gamepads.Clear();   // UniquePtr dtors close each SDL handle
            m_mouse.ReleaseCursors();
        }

        [[nodiscard]] IKeyboard* Keyboard() override { return &m_keyboard; }
        [[nodiscard]] IMouse*    Mouse()    override { return &m_mouse; }
        [[nodiscard]] ITouch*    Touch()    override { return &m_touch; }
        [[nodiscard]] core::i32    GamepadCount() const override { return static_cast<core::i32>(m_gamepads.Size()); }
        [[nodiscard]] IGamepad*  GetGamepad(core::i32 index) override
        {
            if (index < 0 || static_cast<core::usize>(index) >= m_gamepads.Size()) { return nullptr; }
            return m_gamepads[static_cast<core::usize>(index)].Get();
        }
        [[nodiscard]] core::Span<const InputEvent> Events() const override
        {
            return core::Span<const InputEvent>{ m_events.Data(), m_events.Size() };
        }
        [[nodiscard]] core::u32 HoverWindow()   const override { return m_hoverWindow; }
        [[nodiscard]] core::u32 FocusedWindow() const override { return m_focusWindow; }
        void Update() override
        {
            m_keyboard.BeginFrame();
            m_mouse.BeginFrame();
            for (auto& g : m_gamepads) { g->BeginFrame(); }
            m_events.Clear();   // events are valid only for the frame they were pumped in
        }

        // --- backend wiring (called by the shell event pump) ---
        SDL3Keyboard& KeyboardDevice() noexcept { return m_keyboard; }
        SDL3Mouse&    MouseDevice() noexcept { return m_mouse; }
        SDL3Touch&    TouchDevice() noexcept { return m_touch; }
        void SetWindow(SDL_Window* window) { m_mouse.SetWindow(window); }

        // Emit an input event onto this frame's stream (also apply it to the snapshot at the
        // call site - the snapshot is a fold over these events).
        void EmitEvent(const InputEvent& e) { m_events.PushBack(e); }
        void SetHoverWindow(core::u32 id)   noexcept { m_hoverWindow = id; }
        void SetFocusWindow(core::u32 id)   noexcept { m_focusWindow = id; }

        void AddGamepad(SDL_JoystickID id)
        {
            if (FindGamepadById(id) != nullptr) { return; }
            SDL_Gamepad* pad = SDL_OpenGamepad(id);
            if (pad == nullptr) { return; }

            const char* n = SDL_GetGamepadName(pad);
            core::String name = (n != nullptr)
                ? core::String(core::StringView(reinterpret_cast<const core::utf8char*>(n)))
                : core::String{};
            const core::i32 index = static_cast<core::i32>(m_gamepads.Size());
            m_gamepads.PushBack(core::MakeUnique<SDL3Gamepad>(core::DefaultAllocator(), pad, id, index, static_cast<core::String&&>(name)));
        }

        void RemoveGamepad(SDL_JoystickID id)
        {
            for (core::usize i = 0; i < m_gamepads.Size(); ++i)
            {
                if (m_gamepads[i]->Id() == id)
                {
                    m_gamepads.RemoveAt(i);   // UniquePtr dtor closes the SDL handle
                    for (core::usize j = 0; j < m_gamepads.Size(); ++j) { m_gamepads[j]->SetIndex(static_cast<core::i32>(j)); }
                    return;
                }
            }
        }

        SDL3Gamepad* FindGamepadById(SDL_JoystickID id)
        {
            for (auto& g : m_gamepads) { if (g->Id() == id) { return g.Get(); } }
            return nullptr;
        }

    private:
        SDL3Keyboard m_keyboard;
        SDL3Mouse    m_mouse;
        SDL3Touch    m_touch;
        core::Array<core::UniquePtr<SDL3Gamepad>> m_gamepads;
        core::Array<InputEvent>   m_events;         // this frame's event stream
        core::u32                 m_hoverWindow = 0; // window under the pointer
        core::u32                 m_focusWindow = 0; // keyboard-focused window
    };

    // Native file/folder dialogs over SDL3 (SDL_Show{Open,Save}FileDialog / SDL_ShowOpenFolderDialog).
    // Async: each Show* returns immediately; SDL fires Trampoline later (during SDL event processing,
    // i.e. the shell's ProcessEvents pump), which hands the callback OWNED path copies and frees the
    // heap context. Modeled on Sedulous's SDL3DialogService, with typed filters + owned result paths.
    class SDL3DialogService final : public IDialogService
    {
    public:
        explicit SDL3DialogService(SDL3WindowManager& windows) noexcept : m_windows(&windows) {}

        void ShowOpenFile(DialogResultCallback callback, core::Span<const FileFilter> filters,
                          core::StringView defaultPath, bool allowMultiple, core::u32 parentWindowId) override
        {
            Context* ctx = MakeContext(static_cast<DialogResultCallback&&>(callback), filters, defaultPath);
            SDL_ShowOpenFileDialog(&Trampoline, ctx, ParentHandle(parentWindowId),
                                   ctx->sdlFilters.IsEmpty() ? nullptr : ctx->sdlFilters.Data(),
                                   static_cast<int>(ctx->sdlFilters.Size()),
                                   ctx->defaultPath.IsEmpty() ? nullptr : reinterpret_cast<const char*>(ctx->defaultPath.Data()),
                                   allowMultiple);
        }

        void ShowSaveFile(DialogResultCallback callback, core::Span<const FileFilter> filters,
                          core::StringView defaultPath, core::u32 parentWindowId) override
        {
            Context* ctx = MakeContext(static_cast<DialogResultCallback&&>(callback), filters, defaultPath);
            SDL_ShowSaveFileDialog(&Trampoline, ctx, ParentHandle(parentWindowId),
                                   ctx->sdlFilters.IsEmpty() ? nullptr : ctx->sdlFilters.Data(),
                                   static_cast<int>(ctx->sdlFilters.Size()),
                                   ctx->defaultPath.IsEmpty() ? nullptr : reinterpret_cast<const char*>(ctx->defaultPath.Data()));
        }

        void ShowOpenFolder(DialogResultCallback callback, core::StringView defaultPath,
                            bool allowMultiple, core::u32 parentWindowId) override
        {
            Context* ctx = MakeContext(static_cast<DialogResultCallback&&>(callback), {}, defaultPath);
            SDL_ShowOpenFolderDialog(&Trampoline, ctx, ParentHandle(parentWindowId),
                                     ctx->defaultPath.IsEmpty() ? nullptr : reinterpret_cast<const char*>(ctx->defaultPath.Data()),
                                     allowMultiple);
        }

        void OpenPath(core::StringView path) override
        {
            if (path.IsEmpty()) { return; }
            // SDL_OpenURL routes a file:// URI to the OS handler (xdg-open / ShellExecute / open),
            // which opens a directory in the system file manager. Build the URI exactly like the
            // proven Sedulous path: normalize backslashes to '/' (so Windows C:\a\b becomes C:/a/b -
            // backslashes in the URI break the Windows handler) and prefix file:/// unconditionally.
            core::String normalized(path);
            normalized.Replace(core::utf8char('\\'), core::utf8char('/'));
            core::String uri(u8"file:///");
            uri += normalized;
            // SDL_OpenURL returns true on success; on failure SDL_GetError() explains why. Log both so
            // "nothing happened" is diagnosable (bad URI, no handler registered, sandbox block, ...).
            const bool ok = SDL_OpenURL(reinterpret_cast<const char*>(uri.CStr()));
            if (ok)
            {
                DRACONIC_LOG_DEBUG(u8"Shell", u8"OpenPath: SDL_OpenURL('{}') ok", uri.AsView());
            }
            else
            {
                const char* err = SDL_GetError();
                DRACONIC_LOG_WARNING(u8"Shell", u8"OpenPath: SDL_OpenURL('{}') failed: {}",
                                     uri.AsView(),
                                     core::StringView(reinterpret_cast<const core::utf8char*>(
                                         (err != nullptr) ? err : "(null)")));
            }
        }

    private:
        // Heap-lived across the dialog's async lifetime; freed in Trampoline. Owns the callback, the
        // null-terminated strings the SDL_DialogFileFilter pointers alias, and the default path.
        struct Context
        {
            DialogResultCallback callback;
            core::Array<core::String> filterStrings;   // name,pattern,name,pattern,... keeps Data() alive
            core::Array<SDL_DialogFileFilter> sdlFilters;
            core::String defaultPath;
        };

        [[nodiscard]] SDL_Window* ParentHandle(core::u32 id) const noexcept
        {
            if (id == 0) { return nullptr; }
            SDL3Window* w = m_windows->Find(id);
            return (w != nullptr) ? w->Handle() : nullptr;
        }

        [[nodiscard]] static Context* MakeContext(DialogResultCallback&& callback,
                                                  core::Span<const FileFilter> filters,
                                                  core::StringView defaultPath)
        {
            Context* ctx = core::DefaultAllocator().New<Context>();
            ctx->callback = static_cast<DialogResultCallback&&>(callback);
            ctx->defaultPath = core::String(defaultPath);   // null-terminated copy for the C API
            // Fill filterStrings FIRST (so the array stops growing), THEN alias sdlFilters at them -
            // otherwise a PushBack realloc would dangle the SDL filter pointers.
            for (const FileFilter& f : filters)
            {
                ctx->filterStrings.PushBack(core::String(f.name));
                ctx->filterStrings.PushBack(core::String(f.pattern));
            }
            for (core::usize i = 0; i < filters.Size(); ++i)
            {
                SDL_DialogFileFilter sf{};
                sf.name    = reinterpret_cast<const char*>(ctx->filterStrings[i * 2 + 0].Data());
                sf.pattern = reinterpret_cast<const char*>(ctx->filterStrings[i * 2 + 1].Data());
                ctx->sdlFilters.PushBack(sf);
            }
            return ctx;
        }

        static void SDLCALL Trampoline(void* userdata, const char* const* filelist, int /*filter*/)
        {
            Context* ctx = static_cast<Context*>(userdata);
            core::Array<core::String> paths;
            if (filelist != nullptr)   // null => cancelled or error
            {
                for (const char* const* p = filelist; *p != nullptr; ++p)
                {
                    paths.PushBack(core::String(core::StringView(reinterpret_cast<const core::utf8char*>(*p))));
                }
            }
            if (ctx->callback) { ctx->callback(core::Span<const core::String>(paths.Data(), paths.Size())); }
            core::DefaultAllocator().Delete(ctx);
        }

        SDL3WindowManager* m_windows;
    };

    class SDL3Shell final : public IShell
    {
    public:
        // Note on the Wayland Vulkan-window quirk: SDL only attaches libdecor
        // client-side decorations to a window backed by a GPU surface, so a plain
        // window comes up bare on GNOME/Mutter. Sdl3WindowFlags() flags every
        // window as Vulkan on Linux (skipped under the "dummy" driver) to fix it.
        explicit SDL3Shell(const WindowSettings& settings = {}) noexcept
        {
            SDL_SetMainReady();
            if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { m_running = false; return; }
            m_initialized = true;

            core::Result<IWindow*> main = m_windows.CreateWindow(settings);
            if (!main.HasValue()) { m_running = false; return; }
            if (SDL3Window* w = m_windows.Find(main.Value()->Id())) { m_input.SetWindow(w->Handle()); }

            // Independent record of the shell's chosen WSI (compare against the RHI's surface-WSI line).
            const char* drv = SDL_GetCurrentVideoDriver();
            core::ConsoleWrite(u8"[Shell] SDL video driver: ");
            core::ConsoleWrite(core::StringView(reinterpret_cast<const core::utf8char*>(drv != nullptr ? drv : "unknown")));
            core::ConsoleWrite(u8" | main window WSI: ");
            core::ConsoleWrite(WindowSystemName(main.Value()->Native().system));
            core::ConsoleWrite(u8"\n");
        }

        ~SDL3Shell() override
        {
            // Release SDL-owned input resources and destroy windows before
            // tearing SDL down (no SDL calls may happen after SDL_Quit).
            m_input.ReleaseDevices();
            m_windows.DestroyAllNow();
            if (m_initialized) { SDL_Quit(); }
        }

        SDL3Shell(const SDL3Shell&) = delete;
        SDL3Shell& operator=(const SDL3Shell&) = delete;

        [[nodiscard]] IWindowManager* WindowManager() noexcept override { return &m_windows; }
        [[nodiscard]] IWindow* MainWindow() noexcept override { return m_windows.MainWindow(); }
        [[nodiscard]] IInputManager* Input() noexcept override { return &m_input; }
        [[nodiscard]] IDialogService* Dialogs() noexcept override { return &m_dialogs; }

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
                    case SDL_EVENT_DROP_FILE:
                    {
                        if (event.drop.data != nullptr)
                        {
                            DroppedFile drop;
                            drop.window = event.drop.windowID;
                            drop.x = event.drop.x;
                            drop.y = event.drop.y;
                            drop.path = core::String(core::StringView(reinterpret_cast<const core::utf8char*>(event.drop.data)));
                            m_droppedFiles.PushBack(core::Move(drop));
                        }
                        break;
                    }

                    case SDL_EVENT_QUIT:
                        // App-level quit: interceptable (unsaved-changes prompts) like the
                        // main window's close button below.
                        if (OnMainWindowCloseRequested && !OnMainWindowCloseRequested()) { break; }
                        if (IWindow* main = m_windows.MainWindow())
                        {
                            main->Close();
                            m_windows.PushEvent(WindowEvent{ WindowEventType::CloseRequested, main->Id() });
                        }
                        m_running = false;
                        break;
                    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    {
                        const core::u32 id = static_cast<core::u32>(event.window.windowID);
                        // MAIN window close is interceptable; when vetoed, nothing happens
                        // (no event, no teardown - the app exits later via the host).
                        IWindow* main = m_windows.MainWindow();
                        if (main != nullptr && main->Id() == id
                            && OnMainWindowCloseRequested && !OnMainWindowCloseRequested())
                        {
                            break;
                        }
                        m_windows.PushEvent(WindowEvent{ WindowEventType::CloseRequested, id });
                        // Closing the main window stops the shell; the
                        // Application handles secondary-window close via the event.
                        if (main != nullptr && main->Id() == id) { main->Close(); m_running = false; }
                        break;
                    }
                    case SDL_EVENT_WINDOW_RESIZED:
                    {
                        const core::u32 id = static_cast<core::u32>(event.window.windowID);
                        if (SDL3Window* w = m_windows.Find(id))
                        {
                            const core::u32 nw = static_cast<core::u32>(event.window.data1);
                            const core::u32 nh = static_cast<core::u32>(event.window.data2);
                            w->OnResized(nw, nh);
                            m_windows.PushEvent(WindowEvent{ WindowEventType::Resized, id, nw, nh });
                        }
                        break;
                    }
                    case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    {
                        const core::u32 id = static_cast<core::u32>(event.window.windowID);
                        m_input.SetFocusWindow(id);   // keyboard/gamepad routing authority
                        m_windows.PushEvent(WindowEvent{ WindowEventType::FocusGained, id });
                        break;
                    }
                    case SDL_EVENT_WINDOW_FOCUS_LOST:
                    {
                        const core::u32 id = static_cast<core::u32>(event.window.windowID);
                        if (m_input.FocusedWindow() == id) { m_input.SetFocusWindow(0); }
                        m_windows.PushEvent(WindowEvent{ WindowEventType::FocusLost, id });
                        break;
                    }
                    case SDL_EVENT_WINDOW_MOUSE_ENTER:
                        m_input.SetHoverWindow(static_cast<core::u32>(event.window.windowID));
                        break;
                    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                        if (m_input.HoverWindow() == static_cast<core::u32>(event.window.windowID))
                        {
                            m_input.SetHoverWindow(0);
                        }
                        break;

                    // --- Keyboard --- (emit event, then fold into the snapshot)
                    case SDL_EVENT_KEY_DOWN:
                    case SDL_EVENT_KEY_UP:
                    {
                        InputEvent e{};
                        e.kind = event.key.down ? InputEventKind::KeyDown : InputEventKind::KeyUp;
                        e.window = static_cast<core::u32>(event.key.windowID);
                        e.key = MapKeyCode(event.key.scancode);
                        e.modifiers = MapModifiers(event.key.mod);
                        if (e.key == KeyCode::Unknown && event.key.down)
                        {
                            const core::String scName(reinterpret_cast<const core::utf8char*>(
                                SDL_GetScancodeName(event.key.scancode)));
                            DRACONIC_LOG_DEBUG(u8"Shell", u8"unmapped key scancode {} ('{}')",
                                static_cast<core::u32>(event.key.scancode), scName);
                        }
                        m_input.EmitEvent(e);
                        m_input.KeyboardDevice().SetKey(e.key, event.key.down);
                        m_input.KeyboardDevice().SetModifiers(e.modifiers);
                        break;
                    }
                    case SDL_EVENT_TEXT_INPUT:   // only arrives after SDL_StartTextInput (focus-driven, later)
                    {
                        InputEvent e{};
                        e.kind = InputEventKind::TextInput;
                        e.window = static_cast<core::u32>(event.text.windowID);
                        if (event.text.text != nullptr)
                        {
                            core::usize n = 0;
                            while (n + 1 < sizeof(e.text) && event.text.text[n] != '\0')
                            {
                                e.text[n] = static_cast<core::utf8char>(event.text.text[n]); ++n;
                            }
                            e.text[n] = static_cast<core::utf8char>('\0');
                        }
                        m_input.EmitEvent(e);
                        break;
                    }

                    // --- Mouse ---
                    case SDL_EVENT_MOUSE_MOTION:
                    {
                        m_input.SetHoverWindow(static_cast<core::u32>(event.motion.windowID));
                        InputEvent e{};
                        e.kind = InputEventKind::MouseMove;
                        e.window = static_cast<core::u32>(event.motion.windowID);
                        e.x = event.motion.x; e.y = event.motion.y;
                        e.dx = event.motion.xrel; e.dy = event.motion.yrel;
                        m_input.EmitEvent(e);
                        m_input.MouseDevice().OnMotion(event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel);
                        break;
                    }
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    case SDL_EVENT_MOUSE_BUTTON_UP:
                    {
                        const core::u32 btn = static_cast<core::u32>(event.button.button) - 1;
                        const bool down = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                        InputEvent e{};
                        e.kind = down ? InputEventKind::MouseButtonDown : InputEventKind::MouseButtonUp;
                        e.window = static_cast<core::u32>(event.button.windowID);
                        e.button = MapMouseButton(btn);
                        e.x = event.button.x; e.y = event.button.y;
                        m_input.EmitEvent(e);
                        m_input.MouseDevice().OnButton(btn, down);
                        break;
                    }
                    case SDL_EVENT_MOUSE_WHEEL:
                    {
                        InputEvent e{};
                        e.kind = InputEventKind::MouseWheel;
                        e.window = static_cast<core::u32>(event.wheel.windowID);
                        e.x = event.wheel.x; e.y = event.wheel.y;
                        m_input.EmitEvent(e);
                        m_input.MouseDevice().OnWheel(event.wheel.x, event.wheel.y);
                        break;
                    }

                    // --- Touch ---
                    case SDL_EVENT_FINGER_DOWN:
                    case SDL_EVENT_FINGER_MOTION:
                    {
                        InputEvent e{};
                        e.kind = (event.type == SDL_EVENT_FINGER_DOWN) ? InputEventKind::TouchDown : InputEventKind::TouchMove;
                        e.window = static_cast<core::u32>(event.tfinger.windowID);
                        e.touchId = static_cast<core::u64>(event.tfinger.fingerID);
                        e.x = event.tfinger.x; e.y = event.tfinger.y; e.value = event.tfinger.pressure;
                        m_input.EmitEvent(e);
                        m_input.TouchDevice().AddOrUpdate(TouchPoint{ e.touchId, e.x, e.y, e.value });
                        break;
                    }
                    case SDL_EVENT_FINGER_UP:
                    {
                        InputEvent e{};
                        e.kind = InputEventKind::TouchUp;
                        e.window = static_cast<core::u32>(event.tfinger.windowID);
                        e.touchId = static_cast<core::u64>(event.tfinger.fingerID);
                        e.x = event.tfinger.x; e.y = event.tfinger.y;
                        m_input.EmitEvent(e);
                        m_input.TouchDevice().Remove(e.touchId);
                        break;
                    }

                    // --- Gamepad --- (tagged with the focused window; pads aren't window-bound)
                    case SDL_EVENT_GAMEPAD_ADDED:
                        m_input.AddGamepad(event.gdevice.which);
                        break;
                    case SDL_EVENT_GAMEPAD_REMOVED:
                        m_input.RemoveGamepad(event.gdevice.which);
                        break;
                    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    case SDL_EVENT_GAMEPAD_BUTTON_UP:
                        if (SDL3Gamepad* pad = m_input.FindGamepadById(event.gbutton.which))
                        {
                            const GamepadButton b = MapGamepadButton(static_cast<SDL_GamepadButton>(event.gbutton.button));
                            if (b != GamepadButton::Count)
                            {
                                const bool down = (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
                                InputEvent e{};
                                e.kind = down ? InputEventKind::GamepadButtonDown : InputEventKind::GamepadButtonUp;
                                e.window = m_input.FocusedWindow();
                                e.gamepad = pad->Index(); e.padButton = b;
                                m_input.EmitEvent(e);
                                pad->SetButton(b, down);
                            }
                        }
                        break;
                    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                        if (SDL3Gamepad* pad = m_input.FindGamepadById(event.gaxis.which))
                        {
                            const GamepadAxis a = MapGamepadAxis(static_cast<SDL_GamepadAxis>(event.gaxis.axis));
                            if (a != GamepadAxis::Count)
                            {
                                InputEvent e{};
                                e.kind = InputEventKind::GamepadAxis;
                                e.window = m_input.FocusedWindow();
                                e.gamepad = pad->Index(); e.padAxis = a;
                                e.value = static_cast<core::f32>(event.gaxis.value) / 32767.0f;   // snapshot reads axes live
                                m_input.EmitEvent(e);
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
            IWindow* main = m_windows.MainWindow();   // MainWindow() is const now - no const_cast needed
            return m_running && main != nullptr && main->IsOpen();
        }

            void DrainDroppedFiles(core::Array<DroppedFile>& out) override
        {
            for (DroppedFile& drop : m_droppedFiles) { out.PushBack(core::Move(drop)); }
            m_droppedFiles.Clear();
        }

        void RequestExit() override { m_running = false; }

        void SetClipboardText(core::StringView text) override
        {
            const core::String owned(text);   // guarantee null-termination for the C API
            SDL_SetClipboardText(reinterpret_cast<const char*>(owned.Data()));
        }

        [[nodiscard]] core::String GetClipboardText() const override
        {
            char* text = SDL_GetClipboardText();   // never null (empty string on none); caller frees
            core::String result(reinterpret_cast<const core::utf8char*>(text));
            SDL_free(text);
            return result;
        }

        [[nodiscard]] bool HasClipboardText() const noexcept override { return SDL_HasClipboardText(); }

    private:
        static KeyCode MapKeyCode(SDL_Scancode sc) noexcept
        {
            if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
                return static_cast<KeyCode>(static_cast<core::u32>(KeyCode::A) + (sc - SDL_SCANCODE_A));
            if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
                return static_cast<KeyCode>(static_cast<core::u32>(KeyCode::Num1) + (sc - SDL_SCANCODE_1));
            if (sc == SDL_SCANCODE_0) return KeyCode::Num0;
            if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)
                return static_cast<KeyCode>(static_cast<core::u32>(KeyCode::F1) + (sc - SDL_SCANCODE_F1));
            if (sc >= SDL_SCANCODE_F13 && sc <= SDL_SCANCODE_F24)
                return static_cast<KeyCode>(static_cast<core::u32>(KeyCode::F13) + (sc - SDL_SCANCODE_F13));
            if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9)   // SDL keypad digits: 1..9 then 0
                return static_cast<KeyCode>(static_cast<core::u32>(KeyCode::Keypad1) + (sc - SDL_SCANCODE_KP_1));
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
                case SDL_SCANCODE_KP_ENTER:    return KeyCode::KeypadEnter;
                case SDL_SCANCODE_KP_0:        return KeyCode::Keypad0;
                case SDL_SCANCODE_KP_DIVIDE:   return KeyCode::KeypadDivide;
                case SDL_SCANCODE_KP_MULTIPLY: return KeyCode::KeypadMultiply;
                case SDL_SCANCODE_KP_MINUS:    return KeyCode::KeypadMinus;
                case SDL_SCANCODE_KP_PLUS:     return KeyCode::KeypadPlus;
                case SDL_SCANCODE_KP_PERIOD:   return KeyCode::KeypadDecimal;
                case SDL_SCANCODE_MINUS:        return KeyCode::Minus;
                case SDL_SCANCODE_EQUALS:       return KeyCode::Equals;
                case SDL_SCANCODE_LEFTBRACKET:  return KeyCode::LeftBracket;
                case SDL_SCANCODE_RIGHTBRACKET: return KeyCode::RightBracket;
                case SDL_SCANCODE_BACKSLASH:    return KeyCode::Backslash;
                case SDL_SCANCODE_SEMICOLON:    return KeyCode::Semicolon;
                case SDL_SCANCODE_APOSTROPHE:   return KeyCode::Apostrophe;
                case SDL_SCANCODE_GRAVE:        return KeyCode::Grave;
                case SDL_SCANCODE_COMMA:        return KeyCode::Comma;
                case SDL_SCANCODE_PERIOD:       return KeyCode::Period;
                case SDL_SCANCODE_SLASH:        return KeyCode::Slash;
                case SDL_SCANCODE_CAPSLOCK:     return KeyCode::CapsLock;
                case SDL_SCANCODE_SCROLLLOCK:   return KeyCode::ScrollLock;
                case SDL_SCANCODE_NUMLOCKCLEAR: return KeyCode::NumLock;
                case SDL_SCANCODE_PRINTSCREEN:  return KeyCode::PrintScreen;
                case SDL_SCANCODE_PAUSE:        return KeyCode::Pause;
                case SDL_SCANCODE_APPLICATION:  return KeyCode::Menu;
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

        // SDL mouse button number (1-based) minus 1 -> MouseButton (Left/Middle/Right/X1/X2).
        static MouseButton MapMouseButton(core::u32 idx) noexcept
        {
            switch (idx)
            {
                case 0: return MouseButton::Left;
                case 1: return MouseButton::Middle;
                case 2: return MouseButton::Right;
                case 3: return MouseButton::X1;
                case 4: return MouseButton::X2;
                default: return MouseButton::Count;
            }
        }

        static GamepadAxis MapGamepadAxis(SDL_GamepadAxis a) noexcept
        {
            switch (a)
            {
                case SDL_GAMEPAD_AXIS_LEFTX:          return GamepadAxis::LeftX;
                case SDL_GAMEPAD_AXIS_LEFTY:          return GamepadAxis::LeftY;
                case SDL_GAMEPAD_AXIS_RIGHTX:         return GamepadAxis::RightX;
                case SDL_GAMEPAD_AXIS_RIGHTY:         return GamepadAxis::RightY;
                case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:   return GamepadAxis::LeftTrigger;
                case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:  return GamepadAxis::RightTrigger;
                default:                              return GamepadAxis::Count;  // unmapped
            }
        }

        SDL3WindowManager m_windows;
        SDL3InputManager m_input;
        SDL3DialogService m_dialogs{ m_windows };   // ctor takes m_windows (declared above -> init order OK)
        bool m_initialized = false;
        bool m_running = true;
        core::Array<DroppedFile> m_droppedFiles;   // queued during ProcessEvents, drained per frame
    };

    // Factory the DRACONIC_APP_MAIN entry point calls to create the shell.
    [[nodiscard]] core::UniquePtr<IShell> CreateShell(const WindowSettings& settings = {})
    {
        IShell* shell = core::DefaultAllocator().New<SDL3Shell>(settings);
        return core::UniquePtr<IShell>(shell, core::DefaultAllocator());
    }
}
