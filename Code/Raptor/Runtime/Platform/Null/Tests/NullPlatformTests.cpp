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
    CHECK(platform.MainWindow()->Native().system == WindowSystem::Unknown);  // headless: no handles
    CHECK(platform.MainWindow()->Native().window == nullptr);
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

TEST_CASE("platform.null: input is present and reports no activity")
{
    NullPlatform platform;
    IInputManager* input = platform.Input();
    REQUIRE(input != nullptr);

    // Devices are reachable so callers need no null checks.
    REQUIRE(input->Keyboard() != nullptr);
    REQUIRE(input->Mouse() != nullptr);
    REQUIRE(input->Touch() != nullptr);

    CHECK_FALSE(input->Keyboard()->IsKeyDown(KeyCode::Space));
    CHECK(input->Keyboard()->Modifiers() == KeyModifiers::None);
    CHECK(input->Mouse()->X() == 0.0f);
    CHECK_FALSE(input->Mouse()->IsButtonDown(MouseButton::Left));
    CHECK(input->Mouse()->CursorVisible());
    CHECK_FALSE(input->Touch()->HasTouch());
    CHECK(input->GamepadCount() == 0);
    CHECK(input->GetGamepad(0) == nullptr);

    input->Update();  // must be a harmless no-op
}
