#include <doctest/doctest.h>

#include "Core/Prelude.h"

#include <cstdlib>           // setenv
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>        // synthetic events + window id (test exercises the close path)

import raptor.core;
import raptor.runtime.platform;
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
        // Even the dummy driver failed to initialize — nothing to assert.
        MESSAGE("SDL video init/window creation unavailable; skipping");
        CHECK_FALSE(platform.IsRunning());
        return;
    }

    CHECK(platform.MainWindow()->Width() == 640u);
    CHECK(platform.MainWindow()->Height() == 480u);
    CHECK(platform.MainWindow()->NativeHandle() != nullptr);  // SDL_Window*
    CHECK(platform.IsRunning());

    platform.ProcessEvents();   // pump (no pending events) — must not change state
    CHECK(platform.IsRunning());
}

TEST_CASE("platform.desktop: a window-close event stops the platform")
{
    SDL3Platform platform;
    if (platform.MainWindow() == nullptr) { return; }  // no SDL video

    auto* window = static_cast<SDL_Window*>(platform.MainWindow()->NativeHandle());
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
