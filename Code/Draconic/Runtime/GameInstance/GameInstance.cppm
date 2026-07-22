// Draconic::RuntimeGameInstance - the `draconic.runtime.gameinstance` module.
//
// A single RUNNING GAME as a first-class object (docs/design/game-instance.md): its scene pairing,
// its script run context + `Game` object + error sink, and its instance time scale. The player owns
// ONE (N=1); the editor will own an Array<GameInstance> (multi-instance play-in-editor + an in-editor
// headless dedicated server for networking.md). This extracts the run bracket the player and the
// editor's Game tab hand-roll identically today.
//
// Phase 1: owns the SCRIPT run state + bracket + the instance time-scale term; the scene is still
// created by the caller and paired in via SetScene (scene ownership + the create/load/Start/Stop/
// destroy sequence move here in a later phase). BORROWS the ScriptSubsystem and the app's subsystems
// (never owns them).

module;
#include "Core/Prelude.h"

export module draconic.runtime.gameinstance;

import draconic.core;
import draconic.scene;
import draconic.script;
import draconic.script.subsystem;
import draconic.net.manager;        // NetworkManager + INetworkController + NetScriptBinding

using namespace draconic::core;

export namespace draconic::runtime {

namespace dscene = draconic::scene;
namespace dscript = draconic::script;
namespace dnet = draconic::net;

// A hook the app sets once and GameInstance fires on every go-online, passing the freshly created
// endpoint. The app uses it to wire per-endpoint setup that needs app state (e.g. the prefab
// net-spawn resolver, which needs the content DB) - fresh each time, so reconnect stays correct.
using EndpointOnlineHook = core::Function<void(dnet::NetworkManager&)>;

// A running game owns its networking too: GameInstance IS the INetworkController the Net facade
// drives (startServer/connect/disconnect) and reads through. The endpoint is created on demand and
// destroyed on disconnect / instance teardown; the binding is a stable member, so it never dangles.
class GameInstance final : public dnet::INetworkController {
public:
    void SetScene(dscene::Scene* scene) noexcept
    {
        m_scene = scene;
        if (m_net) { m_net->SetReplicatedScene(scene); }   // keep replication on the current scene across level loads
    }
    [[nodiscard]] dscene::Scene* GetScene() const noexcept { return m_scene; }

    void SetScriptErrorHandler(dscript::IScriptErrorHandler* handler) noexcept { m_errorHandler = handler; }
    [[nodiscard]] dscript::IScriptErrorHandler* ScriptErrorHandler() const noexcept { return m_errorHandler; }

    /// Headless: simulate + run scripts, but the host does NOT render this instance (no camera/
    /// swapchain needed) - the in-editor dedicated server (game-instance.md §11 / networking.md).
    void SetHeadless(bool headless) noexcept { m_headless = headless; }
    [[nodiscard]] bool IsHeadless() const noexcept { return m_headless; }

    /// This run's global time scale - the `instance` term in the generalized time model
    /// (dt a scene sees = host dt x context scale x INSTANCE scale x scene scale). Defaults to 1,
    /// so at N=1 it collapses to the previous two-level model (no behaviour change).
    void SetInstanceTimeScale(f32 scale) noexcept { m_instanceTimeScale = scale; }
    [[nodiscard]] f32 InstanceTimeScale() const noexcept { return m_instanceTimeScale; }

    /// Compile + launch the `Game` script (a class with launch()/update(dt)/exit()) on THIS instance's
    /// run host (game-instance.md §11.10). The host must be configured first (the app's
    /// ScriptSubsystem::ConfigureRunHost exposes the facades + routing); a bare test just needs a
    /// backend registered. Idempotent start (stops a prior run first). false on compile / no-`Game`.
    bool StartScript(core::StringView source, core::StringView name);

    /// exit() the `Game` + release the game-script hold (idempotent; the update-fault path lands here).
    /// The run host tears down when nothing else pins it (the scene-stop observer drives that).
    void StopScript();

    /// Create a scene in this instance's group AND bind its behaviors to this instance's run host
    /// (game-instance.md §11.10). Use this instead of Scenes().CreateScene so the re-bind happens.
    dscene::Scene* CreateScene(core::StringView name);
    /// Destroy a scene in this instance's group.
    void DestroyScene(dscene::Scene* scene) { m_sceneManager.DestroyScene(scene); }

    /// Tick the `Game` script with gameplay time: hostDt x contextScale x instanceScale x sceneScale.
    /// A faulting update disables THIS instance's script (drops the `Game`), not the app.
    void TickScript(f32 hostDeltaTime, f32 contextTimeScale);

    /// Drive this instance's run host each frame: advance the script binding clock (Time.now/delta)
    /// and step its GC. The subsystem drives its OWN (editor) host; each instance drives its own.
    void DriveRunHost(f32 deltaTime);

    [[nodiscard]] bool ScriptRunning() const noexcept { return m_game.Get() != nullptr; }
    [[nodiscard]] dscript::IScriptContext* ScriptContext() const noexcept { return m_scriptContext.Get(); }

    /// This run's script host - the gameplay context shared by the game script AND this instance's
    /// scenes' behaviors ("one gameplay context per instance", game-instance.md §11). Owned HERE now;
    /// the ScriptSubsystem borrows it (a later step has the instance drive it directly). Moving the
    /// storage onto the instance is the prerequisite for per-instance runs (Array<GameInstance>).
    [[nodiscard]] dscript::ScriptRunHost& RunHost() noexcept { return m_runHost; }

    /// This run's scene group (game-instance.md §11.2 / §4.4): the set of scenes the run manages + its
    /// current scene, ticked on the Context lane once registered with the SceneSubsystem. Wire its
    /// aware-registry (from the SceneSubsystem) before creating scenes in it.
    [[nodiscard]] dscene::SceneManager& Scenes() noexcept { return m_sceneManager; }

    // ---- networking (this instance's endpoint; INetworkController for the Net facade) ----

    /// A hook the app sets once; GameInstance fires it (with the live endpoint) each time the game
    /// goes online, so the app can wire per-endpoint setup that needs app state (the net-spawn
    /// resolver from the content DB). Not consumed - reconnect re-runs it.
    void SetEndpointOnlineHook(EndpointOnlineHook hook) { m_onEndpointOnline = static_cast<EndpointOnlineHook&&>(hook); }

    /// Drive this instance's networking on the FIXED lane (deterministic step): pump the socket,
    /// route RPCs + replication, push per-peer deltas (server) / sample interpolation (client).
    /// No-op when offline. Called by the app's fixed-step fan-out.
    void DriveNetwork(f32 fixedDeltaMs);

    // INetworkController - the Net facade calls these. StartServer/Connect open a real UDP socket
    // and enter the role (returning false if it fails); StopNetworking drops the endpoint. The live
    // endpoint replicates THIS instance's current scene.
    bool StartServer(u16 port, bool dedicated) override;
    bool Connect(core::StringView host, u16 port) override;
    void StopNetworking() override;
    [[nodiscard]] dnet::NetworkManager* NetEndpoint() const override { return m_net.Get(); }

private:
    /// Point this instance's script context at its net binding (call once the context exists), so the
    /// Net facade resolves THIS instance's controller. Idempotent; safe when the context is null.
    void InstallNetBinding();

    dscript::ScriptRunHost m_runHost;      // owned: the game's script context (§11.10)
    dscene::SceneManager   m_sceneManager; // owned; registered with the SceneSubsystem to tick
    dscene::Scene* m_scene = nullptr;
    dscript::IScriptErrorHandler* m_errorHandler = nullptr;
    bool m_headless = false;
    f32 m_instanceTimeScale = 1.0f;
    core::RefPtr<dscript::IScriptContext> m_scriptContext;   // the game script's ref to the run host's context
    core::RefPtr<dscript::ScriptObject> m_game;

    core::UniquePtr<dnet::NetworkManager> m_net;   // this instance's endpoint (null = offline)
    dnet::NetScriptBinding m_netBinding;           // stable; the facade resolves controller=this
    EndpointOnlineHook m_onEndpointOnline;         // app-set; fires with m_net on each go-online
};

}
