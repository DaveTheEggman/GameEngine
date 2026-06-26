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

TEST_CASE("platform.null: window manager creates, lists, and looks up windows")
{
    NullPlatform platform;
    IWindowManager* wm = platform.WindowManager();
    REQUIRE(wm != nullptr);

    // The platform seeds one main window; it is Windows()[0] and MainWindow().
    REQUIRE(wm->Windows().Size() == 1u);
    IWindow* main = wm->MainWindow();
    REQUIRE(main != nullptr);
    CHECK(wm->Windows()[0] == main);
    CHECK(main->Id() != 0u);                       // 0 is never a valid id
    CHECK(wm->GetWindow(main->Id()) == main);
    CHECK(wm->GetWindow(99999u) == nullptr);

    // Open a second window; ids are distinct, main is unchanged.
    WindowSettings s; s.width = 320; s.height = 240;
    Result<IWindow*> second = wm->CreateWindow(s);
    REQUIRE(second.HasValue());
    CHECK(second.Value()->Id() != main->Id());
    CHECK(wm->Windows().Size() == 2u);
    CHECK(wm->MainWindow() == main);               // still the first
    CHECK(second.Value()->Width() == 320u);
}

TEST_CASE("platform.null: DestroyWindow defers until FlushDestroyed")
{
    NullPlatform platform;
    IWindowManager* wm = platform.WindowManager();
    Result<IWindow*> second = wm->CreateWindow(WindowSettings{});
    REQUIRE(second.HasValue());
    const u32 secondId = second.Value()->Id();
    REQUIRE(wm->Windows().Size() == 2u);

    // Destroy is deferred: the window stays listed (but closed) until flush.
    wm->DestroyWindow(second.Value());
    CHECK(wm->Windows().Size() == 2u);
    CHECK_FALSE(second.Value()->IsOpen());

    wm->FlushDestroyed();
    CHECK(wm->Windows().Size() == 1u);
    CHECK(wm->GetWindow(secondId) == nullptr);
    CHECK(wm->MainWindow() != nullptr);            // main survived
}

TEST_CASE("platform.null: window events queue is empty (no OS source)")
{
    NullPlatform platform;
    platform.ProcessEvents();
    CHECK(platform.WindowManager()->Events().Size() == 0u);
}

TEST_CASE("platform.null: NullWindow resize hook updates reported size")
{
    NullPlatform platform;
    auto* main = static_cast<NullWindow*>(platform.MainWindow());
    main->Resize(1024, 768);
    CHECK(main->Width() == 1024u);
    CHECK(main->Height() == 768u);
    main->SetMinimized(true);
    CHECK(main->IsMinimized());
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
