// Engine::Input tests - the ENGINE-level input surface: the InputSubsystem
// (per-surface scene binding / source overrides) and the per-context Wren Input facade.
//
// Split out of Foundation/Input.Tests: these exercise engine.input (+ the script
// backend), so they live in the Engine collection - Foundation must not link Engine. The
// pure foundation action-model/rebind/interaction/reflection tests stay in Foundation.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.shell;
import foundation.input;
import foundation.settings;
import foundation.script;
#ifdef OPTION_HAS_WREN
import foundation.script.wren;
#endif
import engine.input;

using namespace foundation::core;
using namespace engine::input;
using namespace foundation::input;
namespace shell = foundation::shell;

#include "InputTestSupport.h"

#ifdef OPTION_HAS_WREN
TEST_CASE("input: the Wren Input facade resolves PER-CONTEXT services")
{
    engine::input::RegisterInputScriptFacade();

    // Two runtimes, two contexts - each script reads ITS OWN bound runtime (players /
    // editor-vs-game). No process globals anywhere.
    ActionRuntime runtimeA;
    runtimeA.SetMap(MakeGameplayMap());
    runtimeA.DisableSet(u8"Menu");
    ActionRuntime runtimeB;
    runtimeB.SetMap(MakeGameplayMap());
    runtimeB.DisableSet(u8"Menu");

    FakeDevices devices;
    devices.keyboard.Set(shell::KeyCode::Space, true);
    devices.keyboard.Set(shell::KeyCode::W, true);
    runtimeA.Update(devices, 1.0f / 60.0f); // A sees the press...
    FakeDevices idle;
    runtimeB.Update(idle, 1.0f / 60.0f); // ...B sees nothing

    RefPtr<foundation::script::IScriptManager> manager =
        foundation::script::wren::CreateScriptManager();
    foundation::script::RegisterReflectedTypes(*manager);

    RefPtr<foundation::script::IScriptContext> ctxA = manager->CreateContext();
    RefPtr<foundation::script::IScriptContext> ctxB = manager->CreateContext();
    RefPtr<foundation::script::IScriptContext> ctxNone = manager->CreateContext();
    REQUIRE(ctxA.Get() != nullptr);
    ctxA->SetService(foundation::input::kInputScriptService, &runtimeA);
    ctxB->SetService(foundation::input::kInputScriptService, &runtimeB);

    const StringView script = u8"var Down = Input.isDown(\"Jump\")\n"
                              u8"var MoveY = Input.valueY(\"Move\")\n";
    REQUIRE(ctxA->Load(script, u8"main").IsOk());
    CHECK(ctxA->GetGlobal(u8"Down").Get<bool>() == true);
    CHECK(ctxA->GetGlobal(u8"MoveY").Get<f64>() == doctest::Approx(1.0));

    REQUIRE(ctxB->Load(script, u8"main").IsOk());
    CHECK(ctxB->GetGlobal(u8"Down").Get<bool>() == false); // B's runtime saw nothing
    CHECK(ctxB->GetGlobal(u8"MoveY").Get<f64>() == doctest::Approx(0.0));

    // No service bound: released, never a crash.
    REQUIRE(ctxNone->Load(script, u8"main").IsOk());
    CHECK(ctxNone->GetGlobal(u8"Down").Get<bool>() == false);

    // Script-driven exclusive push lands on the CONTEXT's runtime only.
    runtimeA.EnableSet(u8"Menu");
    REQUIRE(ctxA->Load(u8"Input.pushSet(\"Menu\")\n", u8"main").IsOk());
    CHECK(runtimeA.ExclusiveDepth() == 1);
    CHECK(runtimeB.ExclusiveDepth() == 0);
}
#endif // OPTION_HAS_WREN

TEST_CASE("input.subsystem: the per-surface scene binding rides the source override")
{
    // game-ui.md §9: SetSourceProvider carries the scene the source REPRESENTS (an
    // opaque key - the SceneOverlayView::sceneKey convention); the UI pump confines
    // routing/consumption to it. Un-bound sources follow the policy knob.
    InputSubsystem input(nullptr);
    CHECK(input.BoundSceneKey() == nullptr);
    CHECK(input.UnboundScenePolicy() == UnboundInputScenePolicy::AllScenes); // player default

    FakeDevices devices;
    int sceneStandIn = 0; // any stable address works as a key
    input.SetSourceProvider(&devices, &sceneStandIn);
    CHECK(input.BoundSceneKey() == &sceneStandIn);
    CHECK(&input.ActiveSource() == static_cast<IInputSourceProvider*>(&devices));

    // Re-setting without a scene un-binds (the Game tab's Stop: provider stays, the
    // run's binding drops).
    input.SetSourceProvider(&devices, nullptr);
    CHECK(input.BoundSceneKey() == nullptr);

    // Clearing the provider always clears the binding - the shell source is un-bound.
    input.SetSourceProvider(&devices, &sceneStandIn);
    input.SetSourceProvider(nullptr, &sceneStandIn);
    CHECK(input.BoundSceneKey() == nullptr);

    input.SetUnboundScenePolicy(UnboundInputScenePolicy::ScreenTierOnly);
    CHECK(input.UnboundScenePolicy() == UnboundInputScenePolicy::ScreenTierOnly);
}

TEST_CASE("input.subsystem: ClearSourceProviderIf drops only its own dangling override")
{
    // A closing editor Game tab clears its viewport source before it is destroyed, so
    // ActiveSource()/Update never dereference freed memory. The guard makes it a no-op
    // when a DIFFERENT still-open tab is the active source.
    InputSubsystem input(nullptr);
    FakeDevices tabA;
    FakeDevices tabB;
    int keyA = 0;

    input.SetSourceProvider(&tabA, &keyA);
    CHECK(&input.ActiveSource() == static_cast<IInputSourceProvider*>(&tabA));

    // Closing a NON-active tab must not disturb the active override.
    input.ClearSourceProviderIf(&tabB);
    CHECK(&input.ActiveSource() == static_cast<IInputSourceProvider*>(&tabA));
    CHECK(input.BoundSceneKey() == &keyA);

    // Closing the ACTIVE tab clears the override + its binding; ActiveSource falls back
    // to the (always-valid) shell source, so the next pump can't dangle.
    input.ClearSourceProviderIf(&tabA);
    CHECK(&input.ActiveSource() != static_cast<IInputSourceProvider*>(&tabA));
    CHECK(input.BoundSceneKey() == nullptr);

    // Idempotent once cleared.
    input.ClearSourceProviderIf(&tabA);
    CHECK(&input.ActiveSource() != static_cast<IInputSourceProvider*>(&tabA));
}
