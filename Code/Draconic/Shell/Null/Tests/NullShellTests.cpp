#include <doctest/doctest.h>

#include "Core/Prelude.h"

import draconic.core;
import draconic.shell;
import draconic.shell.null;

using namespace draconic::core;
using namespace draconic::shell;

TEST_CASE("shell.null: a headless shell reports a window and run state")
{
    WindowSettings settings;
    settings.width = 800;
    settings.height = 600;

    NullShell shell(settings);
    REQUIRE(shell.MainWindow() != nullptr);
    CHECK(shell.MainWindow()->Width() == 800u);
    CHECK(shell.MainWindow()->Height() == 600u);
    CHECK(shell.MainWindow()->Native().system == WindowSystem::Unknown);  // headless: no handles
    CHECK(shell.MainWindow()->Native().window == nullptr);
    CHECK(shell.IsRunning());

    shell.ProcessEvents();  // no-op, must not change run state
    CHECK(shell.IsRunning());

    shell.RequestExit();
    CHECK_FALSE(shell.IsRunning());
}

TEST_CASE("shell.null: closing the window stops the shell")
{
    NullShell shell;
    CHECK(shell.IsRunning());
    shell.MainWindow()->Close();
    CHECK_FALSE(shell.MainWindow()->IsOpen());
    CHECK_FALSE(shell.IsRunning());
}

TEST_CASE("shell.null: window manager creates, lists, and looks up windows")
{
    NullShell shell;
    IWindowManager* wm = shell.WindowManager();
    REQUIRE(wm != nullptr);

    // The shell seeds one main window; it is Windows()[0] and MainWindow().
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

TEST_CASE("shell.null: DestroyWindow defers until FlushDestroyed")
{
    NullShell shell;
    IWindowManager* wm = shell.WindowManager();
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

TEST_CASE("shell.null: window events queue is empty (no OS source)")
{
    NullShell shell;
    shell.ProcessEvents();
    CHECK(shell.WindowManager()->Events().Size() == 0u);
}

TEST_CASE("shell.null: NullWindow resize hook updates reported size")
{
    NullShell shell;
    auto* main = static_cast<NullWindow*>(shell.MainWindow());
    main->Resize(1024, 768);
    CHECK(main->Width() == 1024u);
    CHECK(main->Height() == 768u);
    main->SetMinimized(true);
    CHECK(main->IsMinimized());
}

TEST_CASE("shell.null: input is present and reports no activity")
{
    NullShell shell;
    IInputManager* input = shell.Input();
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
