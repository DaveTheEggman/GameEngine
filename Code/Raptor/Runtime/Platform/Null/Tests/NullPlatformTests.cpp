#include <doctest/doctest.h>

#include "Core/Prelude.h"

import raptor.core;
import raptor.runtime.platform;
import raptor.runtime.platform.null;

using namespace raptor::core;
using namespace raptor::runtime;

TEST_CASE("platform.null: a headless platform reports a window and run state")
{
    WindowSettings settings;
    settings.width = 800;
    settings.height = 600;

    NullPlatform platform(settings);
    REQUIRE(platform.MainWindow() != nullptr);
    CHECK(platform.MainWindow()->Width() == 800u);
    CHECK(platform.MainWindow()->Height() == 600u);
    CHECK(platform.MainWindow()->NativeHandle() == nullptr);
    CHECK(platform.IsRunning());

    platform.ProcessEvents();  // no-op, must not change run state
    CHECK(platform.IsRunning());

    platform.RequestExit();
    CHECK_FALSE(platform.IsRunning());
}

TEST_CASE("platform.null: closing the window stops the platform")
{
    NullPlatform platform;
    CHECK(platform.IsRunning());
    platform.MainWindow()->Close();
    CHECK_FALSE(platform.MainWindow()->IsOpen());
    CHECK_FALSE(platform.IsRunning());
}
