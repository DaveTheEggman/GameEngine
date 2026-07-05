#include <doctest/doctest.h>

#include "Core/Prelude.h"

#include <cstdlib>           // setenv
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>        // synthetic events + window id (tests exercise the close path)

import draconic.core;
import draconic.runtime;
import draconic.shell;
import draconic.runtime.client;
import draconic.runtime.desktop;   // RunApplication (the desktop runner)
import draconic.shell.desktop;

using namespace draconic::core;
using namespace draconic::runtime;
using namespace draconic::shell;

namespace
{
    // Force SDL's headless "dummy" video driver so these run in CI without a
    // display and never flash a real window. Set before any SDL_Init.
    struct ForceDummyDriver
    {
        ForceDummyDriver() {
#ifdef _WIN32
            _putenv_s("SDL_VIDEODRIVER", "dummy");
#else
            ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
        }
    };
    const ForceDummyDriver g_forceDummy;

    // Exits the run loop after a fixed number of frames.
    class FrameCountApp final : public IApplication
    {
    public:
        int frames = 0;
        void OnUpdate(IApplicationHost& host, f32) override { if (++frames == 5) { host.RequestExit(3); } }
    };
}

TEST_CASE("shell.desktop: SDL3 shell creates a window and reports state")
{
    WindowSettings settings;
    settings.title = u8"Draconic Test";
    settings.width = 640;
    settings.height = 480;

    SDL3Shell shell(settings);
    if (shell.MainWindow() == nullptr)
    {
        MESSAGE("SDL video init/window creation unavailable; skipping");
        CHECK_FALSE(shell.IsRunning());
        return;
    }

    CHECK(shell.MainWindow()->Width() == 640u);
    CHECK(shell.MainWindow()->Height() == 480u);
    CHECK(shell.IsRunning());

    // Under the dummy driver the reported window system is shell-dependent:
    // Linux reports Unknown (no real display), Windows still reports Win32.
    // Native() must be callable and self-consistent either way.
    const NativeWindow native = shell.MainWindow()->Native();
#if DRACONIC_PLATFORM_WINDOWS
    CHECK(native.system == WindowSystem::Win32);
#else
    CHECK(native.system == WindowSystem::Unknown);
#endif

    shell.ProcessEvents();   // pump (no pending events) — must not change state
    CHECK(shell.IsRunning());
}

TEST_CASE("shell.desktop: a window-close event stops the shell")
{
    SDL3Shell shell;
    if (shell.MainWindow() == nullptr) { return; }

    auto* window = static_cast<SDL3Window*>(shell.MainWindow())->Handle();
    REQUIRE(window != nullptr);

    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    event.window.windowID = SDL_GetWindowID(window);
    SDL_PushEvent(&event);

    shell.ProcessEvents();
    CHECK_FALSE(shell.IsRunning());
    CHECK_FALSE(shell.MainWindow()->IsOpen());
}

TEST_CASE("shell.desktop: keyboard events drive double-buffered key state")
{
    SDL3Shell shell;
    if (shell.MainWindow() == nullptr) { return; }

    IInputManager* input = shell.Input();
    REQUIRE(input != nullptr);
    REQUIRE(input->Keyboard() != nullptr);
    IKeyboard* kb = input->Keyboard();

    const SDL_WindowID winId = SDL_GetWindowID(static_cast<SDL3Window*>(shell.MainWindow())->Handle());

    auto pushKey = [winId](bool down) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.windowID = winId;
        e.key.scancode = SDL_SCANCODE_A;
        e.key.down = down;
        e.key.mod = SDL_KMOD_LSHIFT;
        SDL_PushEvent(&e);
    };

    // Frame 1: key goes down -> Down and Pressed this frame.
    pushKey(true);
    shell.ProcessEvents();
    CHECK(kb->IsKeyDown(KeyCode::A));
    CHECK(kb->IsKeyPressed(KeyCode::A));
    CHECK(HasFlag(kb->Modifiers(), KeyModifiers::LeftShift));

    // Frame 2: still held, no longer "pressed this frame".
    shell.ProcessEvents();
    CHECK(kb->IsKeyDown(KeyCode::A));
    CHECK_FALSE(kb->IsKeyPressed(KeyCode::A));

    // Frame 3: key goes up -> Released this frame, no longer down.
    pushKey(false);
    shell.ProcessEvents();
    CHECK_FALSE(kb->IsKeyDown(KeyCode::A));
    CHECK(kb->IsKeyReleased(KeyCode::A));
}

TEST_CASE("shell.desktop: mouse motion and buttons are tracked")
{
    SDL3Shell shell;
    if (shell.MainWindow() == nullptr) { return; }

    IInputManager* input = shell.Input();
    REQUIRE(input != nullptr);
    IMouse* mouse = input->Mouse();
    REQUIRE(mouse != nullptr);

    const SDL_WindowID winId = SDL_GetWindowID(static_cast<SDL3Window*>(shell.MainWindow())->Handle());

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = winId;
    motion.motion.x = 12.0f; motion.motion.y = 34.0f;
    motion.motion.xrel = 12.0f; motion.motion.yrel = 34.0f;
    SDL_PushEvent(&motion);

    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.windowID = winId;
    button.button.button = SDL_BUTTON_LEFT;  // 1-based; maps to MouseButton::Left
    button.button.down = true;
    SDL_PushEvent(&button);

    shell.ProcessEvents();
    CHECK(mouse->X() == 12.0f);
    CHECK(mouse->Y() == 34.0f);
    CHECK(mouse->DeltaX() == 12.0f);
    CHECK(mouse->IsButtonDown(MouseButton::Left));
    CHECK(mouse->IsButtonPressed(MouseButton::Left));

    // Next frame with no events: deltas reset, button still held.
    shell.ProcessEvents();
    CHECK(mouse->DeltaX() == 0.0f);
    CHECK(mouse->IsButtonDown(MouseButton::Left));
    CHECK_FALSE(mouse->IsButtonPressed(MouseButton::Left));
}

TEST_CASE("shell.desktop: cursor state is settable")
{
    SDL3Shell shell;
    if (shell.MainWindow() == nullptr) { return; }

    IMouse* mouse = shell.Input()->Mouse();
    REQUIRE(mouse != nullptr);

    CHECK(mouse->CursorVisible());
    mouse->SetCursorVisible(false);
    CHECK_FALSE(mouse->CursorVisible());
    mouse->SetCursorVisible(true);
    CHECK(mouse->CursorVisible());

    // Exercises the system-cursor cache/mapping across a range of types; under
    // the dummy driver creation may fail, but the calls must be safe either way.
    mouse->SetCursor(CursorType::Pointer);
    mouse->SetCursor(CursorType::Text);
    mouse->SetCursor(CursorType::ResizeNWSE);
    mouse->SetCursor(CursorType::Pointer);  // cached on second use
    mouse->SetCursor(CursorType::Default);
}

TEST_CASE("shell.desktop: input exposes a gamepad list")
{
    SDL3Shell shell;
    if (shell.MainWindow() == nullptr) { return; }

    IInputManager* input = shell.Input();
    REQUIRE(input != nullptr);
    // No physical gamepads under the dummy driver; the list is simply empty.
    CHECK(input->GamepadCount() == 0);
    CHECK(input->GetGamepad(0) == nullptr);
}

TEST_CASE("shell.desktop: RequestExit stops the shell")
{
    SDL3Shell shell;
    if (shell.MainWindow() == nullptr) { return; }
    CHECK(shell.IsRunning());
    shell.RequestExit();
    CHECK_FALSE(shell.IsRunning());
}

TEST_CASE("shell.desktop: RunApplication drives the app until it exits")
{
    SDL3Shell shell;
    if (shell.MainWindow() == nullptr) { return; }

    FrameCountApp app;
    const int code = RunApplication(app, shell);   // the desktop runner (draconic.runtime.desktop)

    CHECK(code == 3);
    CHECK(app.frames == 5);            // RequestExit(3) ended the loop
}
