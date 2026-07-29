// draconic.runtime.gameinstance - the extracted run bracket owns the script run state + time scale.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.runtime.gameinstance;
import draconic.scene;
import draconic.script;
import draconic.script.wren;
import draconic.script.angelscript;
import draconic.net;         // NetSession queries (IsServer/PeerCount)
import draconic.net.manager; // NetworkManager (the endpoint the instance owns)
import draconic.input;       // ActionRuntime / IInputSourceProvider (per-instance input)
import draconic.shell;       // IKeyboard / KeyCode (a minimal fake device)

using namespace draconic::core;
namespace runtime = draconic::runtime;
namespace scene = draconic::scene;
namespace net = draconic::net;
namespace input = draconic::input;
namespace shell = draconic::shell;

namespace
{
    // A minimal input source: one keyboard reporting a single held key, everything else absent.
    class OneKeyKeyboard final : public shell::IKeyboard
    {
    public:
        shell::KeyCode key{};
        bool down = false;
        [[nodiscard]] bool IsKeyDown(shell::KeyCode k) const override { return down && k == key; }
        [[nodiscard]] bool IsKeyPressed(shell::KeyCode k) const override
        {
            return down && k == key;
        }
        [[nodiscard]] bool IsKeyReleased(shell::KeyCode) const override { return false; }
        [[nodiscard]] shell::KeyModifiers Modifiers() const override
        {
            return shell::KeyModifiers::None;
        }
    };
    class OneKeySource final : public input::IInputSourceProvider
    {
    public:
        OneKeyKeyboard keyboard;
        [[nodiscard]] shell::IKeyboard* Keyboard() override { return &keyboard; }
        [[nodiscard]] shell::IMouse* Mouse() override { return nullptr; }
        [[nodiscard]] i32 GamepadCount() const override { return 0; }
        [[nodiscard]] shell::IGamepad* Gamepad(i32) override { return nullptr; }
        [[nodiscard]] shell::ITouch* Touch() override { return nullptr; }
    };
    [[nodiscard]] input::InputMap MakeFireMap(shell::KeyCode key)
    {
        input::InputMap map;
        input::ActionSet set;
        set.name = String(u8"S");
        input::Action a;
        a.name = String(u8"fire");
        a.kind = input::ActionKind::Button;
        input::Binding b;
        b.source = input::BindingSource::Key;
        b.code = static_cast<u32>(key);
        a.bindings.PushBack(b);
        set.actions.PushBack(static_cast<input::Action&&>(a));
        map.sets.PushBack(static_cast<input::ActionSet&&>(set));
        return map;
    }
}

TEST_CASE("game-instance: instance time scale defaults to 1 and is settable; fresh instance idle")
{
    runtime::GameInstance gi;
    CHECK(gi.InstanceTimeScale() == doctest::Approx(1.0f));
    gi.SetInstanceTimeScale(0.5f);
    CHECK(gi.InstanceTimeScale() == doctest::Approx(0.5f));
    CHECK_FALSE(gi.ScriptRunning());
    CHECK(gi.GetScene() == nullptr);
    CHECK(gi.ScriptContext() == nullptr);
    CHECK_FALSE(
        gi.RunHost().IsActive()); // the instance owns its run host (idle until a run starts)

    // The instance owns a usable scene group (its SceneManager).
    CHECK(gi.Scenes().SceneCount() == 0u);
    scene::Scene* level = gi.Scenes().CreateScene(u8"L1");
    REQUIRE(level != nullptr);
    CHECK(gi.Scenes().SceneCount() == 1u);
    CHECK(gi.Scenes().CurrentScene() == level);
}

TEST_CASE("game-instance: each instance's input runtime reads ONLY its own source (per-instance "
          "isolation)")
{
    // The multi-instance-PIE fix: each GameInstance has its OWN ActionRuntime bound to its OWN source,
    // so one tab's keys never reach another tab's game (the shared-runtime bug that flipped the server
    // tab into a client). Same map, same key, two sources - only the source with the key held fires.
    runtime::GameInstance a;
    runtime::GameInstance b;
    OneKeySource srcA;
    srcA.keyboard.key = shell::KeyCode::H;
    srcA.keyboard.down = true; // A holds H
    OneKeySource srcB;
    srcB.keyboard.key = shell::KeyCode::H;
    srcB.keyboard.down = false; // B does not
    a.SetInputSource(&srcA);
    a.SetInputMap(MakeFireMap(shell::KeyCode::H));
    b.SetInputSource(&srcB);
    b.SetInputMap(MakeFireMap(shell::KeyCode::H));

    a.DriveInput(0.016f, 1.0f);
    b.DriveInput(0.016f, 1.0f);

    CHECK(a.InputRuntime().IsDown(a.InputRuntime().Resolve(u8"fire")) == true); // A's source has it
    CHECK(b.InputRuntime().IsDown(b.InputRuntime().Resolve(u8"fire")) ==
          false); // B's does NOT (no cross-feed)

    // Flip which source holds the key: isolation holds the other way too.
    srcA.keyboard.down = false;
    srcB.keyboard.down = true;
    a.DriveInput(0.016f, 1.0f);
    b.DriveInput(0.016f, 1.0f);
    CHECK(a.InputRuntime().IsDown(a.InputRuntime().Resolve(u8"fire")) == false);
    CHECK(b.InputRuntime().IsDown(b.InputRuntime().Resolve(u8"fire")) == true);
}

TEST_CASE("game-instance: each instance owns an independent networked endpoint (server + client "
          "over UDP)")
{
    // The per-instance networking model: a GameInstance IS the INetworkController, opening its OWN
    // real UDP endpoint on StartServer/Connect. Two instances in one process = two isolated endpoints.
    runtime::GameInstance server;
    runtime::GameInstance client;
    CHECK(server.NetEndpoint() == nullptr); // offline until a role is entered

    REQUIRE(server.StartServer(/*port=*/0, /*dedicated=*/true));
    REQUIRE(server.NetEndpoint() != nullptr);
    CHECK(server.NetEndpoint()->Session().IsServer());
    const u16 port = server.NetEndpoint()->BoundPort();
    CHECK(port != 0u);

    REQUIRE(client.Connect(u8"127.0.0.1", port));
    REQUIRE(client.NetEndpoint() != nullptr);
    CHECK(client.NetEndpoint()->Session().IsClient());
    CHECK(server.NetEndpoint() != client.NetEndpoint()); // independent endpoints

    for (int i = 0; i < 400 && server.NetEndpoint()->Session().PeerCount() == 0u; ++i)
    {
        server.DriveNetwork(16.0f);
        client.DriveNetwork(16.0f);
        SleepMilliseconds(1);
    }
    CHECK(server.NetEndpoint()->Session().PeerCount() == 1u);

    client.StopNetworking(); // disconnect drops the endpoint
    CHECK(client.NetEndpoint() == nullptr);
    CHECK(server.NetEndpoint() != nullptr); // the server is unaffected (isolation)
}

TEST_CASE("game-instance: fallback path starts, ticks, and stops a Game script")
{
    RegisterCoreTypes();
    draconic::script::wren::RegisterWrenScriptBackend();

    runtime::GameInstance gi;
    const bool ok = gi.StartScript(u8"class Game {\n"
                                   u8"  construct new() {}\n"
                                   u8"  launch() {}\n"
                                   u8"  update(dt) {}\n"
                                   u8"  exit() {}\n"
                                   u8"}\n",
                                   u8"game.wren");
    REQUIRE(ok);
    CHECK(gi.ScriptRunning());
    CHECK(gi.ScriptContext() != nullptr);
    CHECK(gi.RunHost().IsActive()); // the game script runs on the instance's own run host

    gi.DriveRunHost(0.016f);     // advance the run host clock/GC (must not fault)
    gi.TickScript(0.016f, 1.0f); // must not fault
    CHECK(gi.ScriptRunning());

    gi.StopScript();
    CHECK_FALSE(gi.ScriptRunning());
}

TEST_CASE("game-instance: a debugger suspension in update is not a fault - the script survives")
{
    RegisterCoreTypes();
    draconic::script::angelscript::RegisterAngelScriptBackend();

    runtime::GameInstance gi;
    const bool ok = gi.StartScript(u8"class Game {\n"               // 1
                                   u8"  void launch() {}\n"         // 2
                                   u8"  void update(double dt) {\n" // 3
                                   u8"    int a = 1;\n"             // 4  <- breakpoint
                                   u8"    int b = a + 1;\n"         // 5
                                   u8"  }\n"
                                   u8"  void exit() {}\n"
                                   u8"}\n",
                                   u8"game.as");
    REQUIRE(ok);
    REQUIRE(gi.ScriptRunning());

    draconic::script::IScriptDebugger* debugger = nullptr;
    gi.RunHost().RequestDebugger(Function<void(draconic::script::IScriptDebugger&)>{
        [&debugger](draconic::script::IScriptDebugger& created)
        {
            created.SetBreakpoint(u8"game.as", 4);
            debugger = &created;
        }});
    REQUIRE(debugger != nullptr);

    struct BreakCounter final : draconic::script::IScriptDebuggerListener
    {
        int breaks = 0;
        void OnDebuggerStateChanged(draconic::script::ScriptDebuggerState state) override
        {
            if (state == draconic::script::ScriptDebuggerState::Breakpoint)
            {
                ++breaks;
            }
        }
    };
    BreakCounter counter;
    gi.RunHost().SetExternalDebugListener(&counter);

    // Hitting the breakpoint suspends update MID-CALL. The suspension surfaces as an error
    // result - the regression was TickScript reading it as a fault and killing the game
    // script ("Here" logged once, breakpoint never hit again).
    gi.TickScript(0.016f, 1.0f);
    CHECK(gi.RunHost().IsDebugPaused());
    CHECK(gi.ScriptRunning()); // the script SURVIVES the pause
    CHECK(counter.breaks == 1);

    // Ticking WHILE paused must not start a new update call (the per-frame re-break /
    // locals-flicker regression): no new pause events, still paused, still alive.
    gi.TickScript(0.016f, 1.0f);
    gi.TickScript(0.016f, 1.0f);
    CHECK(counter.breaks == 1);
    CHECK(gi.RunHost().IsDebugPaused());
    CHECK(gi.ScriptRunning());

    // Continue completes the held call; the next tick hits the breakpoint AGAIN.
    debugger->Continue();
    CHECK_FALSE(gi.RunHost().IsDebugPaused());
    CHECK(gi.ScriptRunning());
    gi.TickScript(0.016f, 1.0f);
    CHECK(gi.RunHost().IsDebugPaused());
    CHECK(gi.ScriptRunning());
    CHECK(counter.breaks == 2);

    // Removing the breakpoint while paused: Continue finishes the held call and the next
    // ticks run FREELY (the user's remove-during-pause flow, at the backend level).
    debugger->RemoveBreakpoint(u8"game.as", 4);
    debugger->Continue();
    gi.TickScript(0.016f, 1.0f);
    gi.TickScript(0.016f, 1.0f);
    CHECK_FALSE(gi.RunHost().IsDebugPaused());
    CHECK(gi.ScriptRunning());
    CHECK(counter.breaks == 2);

    gi.RunHost().SetExternalDebugListener(nullptr);
    gi.StopScript();
    CHECK_FALSE(gi.ScriptRunning());
}

TEST_CASE("game-instance: two instances own separate, isolated run-host contexts")
{
    RegisterCoreTypes();
    draconic::script::wren::RegisterWrenScriptBackend();

    const char8_t* src =
        u8"class Game { construct new() {}\n launch() {}\n update(dt) {}\n exit() {}\n}\n";
    runtime::GameInstance a;
    runtime::GameInstance b;
    REQUIRE(a.StartScript(src, u8"game.wren"));
    REQUIRE(b.StartScript(src, u8"game.wren"));

    REQUIRE(a.RunHost().Context() != nullptr);
    REQUIRE(b.RunHost().Context() != nullptr);
    CHECK(a.RunHost().Context() !=
          b.RunHost().Context()); // distinct contexts = no shared script globals

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

    runtime::GameInstance gi;
    const bool ok = gi.StartScript(u8"var X = 1\n", u8"game.wren");
    CHECK_FALSE(ok); // no `Game` class
    CHECK_FALSE(gi.ScriptRunning());
}
