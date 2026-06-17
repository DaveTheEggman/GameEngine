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
        ForceDummyDriver() { ::setenv("SDL_VIDEODRIVER", "dummy", 1); }
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
    settings.title = u"Raptor Test";
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

    // Under the dummy driver no real windowing system is active, so native
    // handles aren't available; real-handle extraction is validated with a real
    // display. Native() must still be callable and self-consistent.
    const NativeWindow native = platform.MainWindow()->Native();
    CHECK(native.system == WindowSystem::Unknown);

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
