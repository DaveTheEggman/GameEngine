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

using namespace draconic::core;

export namespace draconic::runtime {

namespace dscene = draconic::scene;
namespace dscript = draconic::script;

class GameInstance {
public:
    void SetScene(dscene::Scene* scene) noexcept { m_scene = scene; }
    [[nodiscard]] dscene::Scene* GetScene() const noexcept { return m_scene; }

    void SetScriptErrorHandler(dscript::IScriptErrorHandler* handler) noexcept { m_errorHandler = handler; }
    [[nodiscard]] dscript::IScriptErrorHandler* ScriptErrorHandler() const noexcept { return m_errorHandler; }

    /// This run's global time scale - the `instance` term in the generalized time model
    /// (dt a scene sees = host dt x context scale x INSTANCE scale x scene scale). Defaults to 1,
    /// so at N=1 it collapses to the previous two-level model (no behaviour change).
    void SetInstanceTimeScale(f32 scale) noexcept { m_instanceTimeScale = scale; }
    [[nodiscard]] f32 InstanceTimeScale() const noexcept { return m_instanceTimeScale; }

    /// Compile + launch the `Game` script (a class with launch()/update(dt)/exit()). `scripts`
    /// non-null = the subsystem's shared run context (the normal path); null = a self-owned manager,
    /// with `exposeServices` binding the app's per-context script services (the headless fallback).
    /// Idempotent start (stops a prior run first). false on any compile / no-`Game`-class failure.
    bool StartScript(dscript::ScriptSubsystem* scripts,
                     const core::Function<void(dscript::IScriptContext&)>& exposeServices,
                     core::StringView source, core::StringView name);

    /// exit() the `Game` + tear down the run context (idempotent; the update-fault path lands here).
    void StopScript(dscript::ScriptSubsystem* scripts);

    /// Tick the `Game` script with gameplay time: hostDt x contextScale x instanceScale x sceneScale.
    /// A faulting update disables THIS instance's script (drops the `Game`), not the app.
    void TickScript(f32 hostDeltaTime, f32 contextTimeScale);

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

private:
    dscript::ScriptRunHost m_runHost;      // owned; borrowed by the ScriptSubsystem (see RunHost())
    dscene::SceneManager   m_sceneManager; // owned; registered with the SceneSubsystem to tick
    dscene::Scene* m_scene = nullptr;
    dscript::IScriptErrorHandler* m_errorHandler = nullptr;
    f32 m_instanceTimeScale = 1.0f;
    core::RefPtr<dscript::IScriptManager> m_scriptManager;   // self-owned (fallback) only
    core::RefPtr<dscript::IScriptContext> m_scriptContext;
    core::RefPtr<dscript::ScriptObject> m_game;
};

}
