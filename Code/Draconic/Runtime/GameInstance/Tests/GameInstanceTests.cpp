// draconic.runtime.gameinstance - the extracted run bracket owns the script run state + time scale.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.runtime.gameinstance;
import draconic.scene;
import draconic.script;
import draconic.script.wren;

using namespace draconic::core;
namespace rt = draconic::runtime;
namespace dscene = draconic::scene;

TEST_CASE("game-instance: instance time scale defaults to 1 and is settable; fresh instance idle")
{
    rt::GameInstance gi;
    CHECK(gi.InstanceTimeScale() == doctest::Approx(1.0f));
    gi.SetInstanceTimeScale(0.5f);
    CHECK(gi.InstanceTimeScale() == doctest::Approx(0.5f));
    CHECK_FALSE(gi.ScriptRunning());
    CHECK(gi.GetScene() == nullptr);
    CHECK(gi.ScriptContext() == nullptr);
    CHECK_FALSE(gi.RunHost().IsActive());   // the instance owns its run host (idle until a run starts)

    // The instance owns a usable scene group (its SceneManager).
    CHECK(gi.Scenes().SceneCount() == 0u);
    dscene::Scene* level = gi.Scenes().CreateScene(u8"L1");
    REQUIRE(level != nullptr);
    CHECK(gi.Scenes().SceneCount() == 1u);
    CHECK(gi.Scenes().CurrentScene() == level);
}

TEST_CASE("game-instance: fallback path starts, ticks, and stops a Game script")
{
    RegisterCoreTypes();
    draconic::script::wren::RegisterWrenScriptBackend();

    rt::GameInstance gi;
    const bool ok = gi.StartScript(
        /*scripts=*/nullptr, /*exposeServices=*/{},
        u8"class Game {\n"
        u8"  construct new() {}\n"
        u8"  launch() {}\n"
        u8"  update(dt) {}\n"
        u8"  exit() {}\n"
        u8"}\n",
        u8"game.wren");
    REQUIRE(ok);
    CHECK(gi.ScriptRunning());
    CHECK(gi.ScriptContext() != nullptr);

    gi.TickScript(0.016f, 1.0f);   // must not fault
    CHECK(gi.ScriptRunning());

    gi.StopScript(/*scripts=*/nullptr);
    CHECK_FALSE(gi.ScriptRunning());
}

TEST_CASE("game-instance: a missing Game class fails to start cleanly")
{
    RegisterCoreTypes();
    draconic::script::wren::RegisterWrenScriptBackend();

    rt::GameInstance gi;
    const bool ok = gi.StartScript(nullptr, {}, u8"var X = 1\n", u8"game.wren");
    CHECK_FALSE(ok);              // no `Game` class
    CHECK_FALSE(gi.ScriptRunning());
}
