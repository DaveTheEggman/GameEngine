#include <doctest/doctest.h>

#include "Core/Prelude.h"

#include <cstdlib>           // setenv
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>        // synthetic events + window id (tests exercise the close path)

import raptor.core;
import raptor.runtime;
import raptor.runtime.platform;
import raptor.runtime.client;
import raptor.runtime.platform.desktop;

using namespace raptor::core;
using namespace raptor::runtime;

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
    class FrameCountApp final : public Application
    {
    public:
        int frames = 0;
    protected:
        void OnUpdate(f32) override { if (++frames == 5) { RequestExit(3); } }
    };
}

TEST_CASE("platform.desktop: SDL3 platform creates a window and reports state")
{
    WindowSettings settings;
    settings.title = u8"Raptor Test";
    settings.width = 640;
    settings.height = 480;

    SDL3Platform platform(settings);
    if (platform.MainWindow() == nullptr)
    {
        MESSAGE("SDL video init/window creation unavailable; skipping");
        CHECK_FALSE(platform.IsRunning());
        return;
    }

    CHECK(platform.MainWindow()->Width() == 640u);
    CHECK(platform.MainWindow()->Height() == 480u);
    CHECK(platform.IsRunning());

    // Under the dummy driver the reported window system is platform-dependent:
    // Linux reports Unknown (no real display), Windows still reports Win32.
    // Native() must be callable and self-consistent either way.
    const NativeWindow native = platform.MainWindow()->Native();
#if RAPTOR_PLATFORM_WINDOWS
    CHECK(native.system == WindowSystem::Win32);
#else
    CHECK(native.system == WindowSystem::Unknown);
#endif

    platform.ProcessEvents();   // pump (no pending events) — must not change state
    CHECK(platform.IsRunning());
}

TEST_CASE("platform.desktop: a window-close event stops the platform")
{
    SDL3Platform platform;
    if (platform.MainWindow() == nullptr) { return; }

    auto* window = static_cast<SDL3Window*>(platform.MainWindow())->Handle();
    REQUIRE(window != nullptr);

    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    event.window.windowID = SDL_GetWindowID(window);
    SDL_PushEvent(&event);

    platform.ProcessEvents();
    CHECK_FALSE(platform.IsRunning());
    CHECK_FALSE(platform.MainWindow()->IsOpen());
}

TEST_CASE("platform.desktop: keyboard events drive double-buffered key state")
{
    SDL3Platform platform;
    if (platform.MainWindow() == nullptr) { return; }

    IInputManager* input = platform.Input();
    REQUIRE(input != nullptr);
    REQUIRE(input->Keyboard() != nullptr);
    IKeyboard* kb = input->Keyboard();

    const SDL_WindowID winId = SDL_GetWindowID(static_cast<SDL3Window*>(platform.MainWindow())->Handle());

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
    platform.ProcessEvents();
    CHECK(kb->IsKeyDown(KeyCode::A));
    CHECK(kb->IsKeyPressed(KeyCode::A));
    CHECK(HasFlag(kb->Modifiers(), KeyModifiers::LeftShift));

    // Frame 2: still held, no longer "pressed this frame".
    platform.ProcessEvents();
    CHECK(kb->IsKeyDown(KeyCode::A));
    CHECK_FALSE(kb->IsKeyPressed(KeyCode::A));

    // Frame 3: key goes up -> Released this frame, no longer down.
    pushKey(false);
    platform.ProcessEvents();
    CHECK_FALSE(kb->IsKeyDown(KeyCode::A));
    CHECK(kb->IsKeyReleased(KeyCode::A));
}

TEST_CASE("platform.desktop: mouse motion and buttons are tracked")
{
    SDL3Platform platform;
    if (platform.MainWindow() == nullptr) { return; }

    IInputManager* input = platform.Input();
    REQUIRE(input != nullptr);
    IMouse* mouse = input->Mouse();
    REQUIRE(mouse != nullptr);

    const SDL_WindowID winId = SDL_GetWindowID(static_cast<SDL3Window*>(platform.MainWindow())->Handle());

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

    platform.ProcessEvents();
    CHECK(mouse->X() == 12.0f);
    CHECK(mouse->Y() == 34.0f);
    CHECK(mouse->DeltaX() == 12.0f);
    CHECK(mouse->IsButtonDown(MouseButton::Left));
    CHECK(mouse->IsButtonPressed(MouseButton::Left));

    // Next frame with no events: deltas reset, button still held.
    platform.ProcessEvents();
    CHECK(mouse->DeltaX() == 0.0f);
    CHECK(mouse->IsButtonDown(MouseButton::Left));
    CHECK_FALSE(mouse->IsButtonPressed(MouseButton::Left));
}

TEST_CASE("platform.desktop: cursor state is settable")
{
    SDL3Platform platform;
    if (platform.MainWindow() == nullptr) { return; }

    IMouse* mouse = platform.Input()->Mouse();
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

TEST_CASE("platform.desktop: input exposes a gamepad list")
{
    SDL3Platform platform;
    if (platform.MainWindow() == nullptr) { return; }

    IInputManager* input = platform.Input();
    REQUIRE(input != nullptr);
    // No physical gamepads under the dummy driver; the list is simply empty.
    CHECK(input->GamepadCount() == 0);
    CHECK(input->GetGamepad(0) == nullptr);
}

TEST_CASE("platform.desktop: RequestExit stops the platform")
{
    SDL3Platform platform;
    if (platform.MainWindow() == nullptr) { return; }
    CHECK(platform.IsRunning());
    platform.RequestExit();
    CHECK_FALSE(platform.IsRunning());
}

TEST_CASE("platform.desktop: RunApplication drives the app until it exits")
{
    SDL3Platform platform;
    if (platform.MainWindow() == nullptr) { return; }

    FrameCountApp app;
    const int code = RunApplication(app, platform);   // the desktop runner lives here

    CHECK(code == 3);
    CHECK(app.frames == 5);            // RequestExit(3) ended the loop
    CHECK_FALSE(app.IsRunning());      // Stop() ran
}
