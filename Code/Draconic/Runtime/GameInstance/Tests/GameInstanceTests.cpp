// draconic.runtime.gameinstance - the extracted run bracket owns the script run state + time scale.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.runtime.gameinstance;
import draconic.scene;
import draconic.script;
import draconic.script.wren;
import draconic.net;            // NetSession queries (IsServer/PeerCount)
import draconic.net.manager;    // NetworkManager (the endpoint the instance owns)

using namespace draconic::core;
namespace rt = draconic::runtime;
namespace dscene = draconic::scene;
namespace dnet = draconic::net;

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

TEST_CASE("game-instance: each instance owns an independent networked endpoint (server + client over UDP)")
{
    // The per-instance networking model: a GameInstance IS the INetworkController, opening its OWN
    // real UDP endpoint on StartServer/Connect. Two instances in one process = two isolated endpoints.
    rt::GameInstance server;
    rt::GameInstance client;
    CHECK(server.NetEndpoint() == nullptr);   // offline until a role is entered

    REQUIRE(server.StartServer(/*port=*/0, /*dedicated=*/true));
    REQUIRE(server.NetEndpoint() != nullptr);
    CHECK(server.NetEndpoint()->Session().IsServer());
    const u16 port = server.NetEndpoint()->BoundPort();
    CHECK(port != 0u);

    REQUIRE(client.Connect(u8"127.0.0.1", port));
    REQUIRE(client.NetEndpoint() != nullptr);
    CHECK(client.NetEndpoint()->Session().IsClient());
    CHECK(server.NetEndpoint() != client.NetEndpoint());   // independent endpoints

    for (int i = 0; i < 400 && server.NetEndpoint()->Session().PeerCount() == 0u; ++i)
    {
        server.DriveNetwork(16.0f); client.DriveNetwork(16.0f); SleepMilliseconds(1);
    }
    CHECK(server.NetEndpoint()->Session().PeerCount() == 1u);

    client.StopNetworking();                       // disconnect drops the endpoint
    CHECK(client.NetEndpoint() == nullptr);
    CHECK(server.NetEndpoint() != nullptr);        // the server is unaffected (isolation)
}

TEST_CASE("game-instance: fallback path starts, ticks, and stops a Game script")
{
    RegisterCoreTypes();
    draconic::script::wren::RegisterWrenScriptBackend();

    rt::GameInstance gi;
    const bool ok = gi.StartScript(
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
    CHECK(gi.RunHost().IsActive());   // the game script runs on the instance's own run host

    gi.DriveRunHost(0.016f);       // advance the run host clock/GC (must not fault)
    gi.TickScript(0.016f, 1.0f);   // must not fault
    CHECK(gi.ScriptRunning());

    gi.StopScript();
    CHECK_FALSE(gi.ScriptRunning());
}

TEST_CASE("game-instance: two instances own separate, isolated run-host contexts")
{
    RegisterCoreTypes();
    draconic::script::wren::RegisterWrenScriptBackend();

    const char8_t* src = u8"class Game { construct new() {}\n launch() {}\n update(dt) {}\n exit() {}\n}\n";
    rt::GameInstance a;
    rt::GameInstance b;
    REQUIRE(a.StartScript(src, u8"game.wren"));
    REQUIRE(b.StartScript(src, u8"game.wren"));

    REQUIRE(a.RunHost().Context() != nullptr);
    REQUIRE(b.RunHost().Context() != nullptr);
    CHECK(a.RunHost().Context() != b.RunHost().Context());   // distinct contexts = no shared script globals

    a.SetHeadless(true);
    CHECK(a.IsHeadless());
    CHECK_FALSE(b.IsHeadless());

    a.StopScript();
    b.StopScript();
    CHECK_FALSE(a.ScriptRunning());
    CHECK_FALSE(b.ScriptRunning());
}

TEST_CASE("game-instance: a missing Game class fails to start cleanly")
{
    RegisterCoreTypes();
    draconic::script::wren::RegisterWrenScriptBackend();

    rt::GameInstance gi;
    const bool ok = gi.StartScript(u8"var X = 1\n", u8"game.wren");
    CHECK_FALSE(ok);              // no `Game` class
    CHECK_FALSE(gi.ScriptRunning());
}
