/// Draconic::SceneSubsystem - `draconic.scene.subsystem`.
///
/// The Context-level scene driver. Since game-instance.md §11 it is a THIN wrapper over the scene
/// lib's SceneManager: it owns the app-wide ISceneAware registry + a DEFAULT SceneManager (the group
/// of loose / editor scenes, i.e. everything not owned by a GameInstance) and drives that manager's
/// per-frame update + fixed update (UpdateOrder -500, so scenes tick before rendering reads them).
/// GameInstances own their OWN SceneManagers over the SAME shared registry (later phases). The scene
/// lifecycle + tick logic all live in SceneManager now; this class just exposes the default manager +
/// the registry to the Context and applies the Context time-scale / fixed-step (draconic.scene stays
/// runtime-free - the manager is Context-agnostic and takes those factors as parameters).

module;
#include "Core/Prelude.h"

export module draconic.scene.subsystem;

import draconic.core;
import draconic.runtime;
import draconic.scene;

using namespace draconic::core;

export namespace draconic::scene {

class SceneSubsystem final : public draconic::runtime::Subsystem {
public:
    SceneSubsystem() noexcept : m_default(&m_registry) {}

    [[nodiscard]] i32 UpdateOrder() const noexcept override { return -500; }   // scenes tick early

    // ---- scene lifecycle (delegates to the default manager) ----

    Scene* CreateScene(StringView name = u8"Scene") { return m_default.CreateScene(name); }
    void DestroyScene(Scene* scene) { m_default.DestroyScene(scene); }
    [[nodiscard]] Scene* GetScene(StringView name) { return m_default.GetScene(name); }
    [[nodiscard]] Span<Scene* const> ActiveScenes() const noexcept { return m_default.ActiveScenes(); }

    /// Visits every live scene in the DEFAULT group (prefab rebuilds after a template save, tooling
    /// sweeps). Per-instance scenes live on their own managers.
    template <typename Fn>
    void ForEachScene(Fn&& fn) { m_default.ForEachScene(static_cast<Fn&&>(fn)); }

    // ---- ISceneAware broker (the registry is app-wide; every manager fans out through it) ----

    void RegisterSceneAware(ISceneAware* aware) { m_registry.Register(aware); }
    void UnregisterSceneAware(ISceneAware* aware) { m_registry.Unregister(aware); }

    /// The shared registry + the default manager, so a GameInstance can build its own SceneManager
    /// over the same app-wide aware list (game-instance.md §11).
    [[nodiscard]] SceneAwareRegistry& AwareRegistry() noexcept { return m_registry; }
    [[nodiscard]] SceneManager& DefaultManager() noexcept { return m_default; }

    // ---- subsystem frame phases (drive the default manager; the Context factors are applied here) ----

    // Fixed stepping is PER SCENE (each scene owns a FixedStepper): BeginFrame runs it so fixed-rate
    // state (physics poses + alpha) is fresh BEFORE any subsystem's Update reads it. BeginFrame gets the
    // RAW host dt (context scale applied inside the manager); Update gets context-scaled dt.
    void BeginFrame(f32 deltaTime) override {
        const f32 contextScale = GetContext() != nullptr ? GetContext()->TimeScale() : 1.0f;
        const f32 contextStep = GetContext() != nullptr ? GetContext()->FixedTimeStep() : 0.0f;
        m_default.BeginFrame(deltaTime, contextScale, contextStep);
    }
    void Update(f32 deltaTime) override { m_default.Update(deltaTime); }
    void OnShutdown() override { m_default.Clear(); }

private:
    SceneAwareRegistry m_registry;         // app-wide aware list (declared first: m_default borrows it)
    SceneManager       m_default;          // the loose / editor scene group
};

} // namespace draconic::scene
