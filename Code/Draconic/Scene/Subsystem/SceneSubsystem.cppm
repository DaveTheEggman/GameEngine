/// Draconic::SceneSubsystem — `draconic.scene.subsystem`.
///
/// The Context-level driver for scenes (the scene MANAGER is the subsystem; the Scene
/// itself is data). Owns the scene list, drives each scene's per-frame update + fixed
/// update (UpdateOrder -500, so scenes tick before rendering reads them), and brokers
/// ISceneAware: when a scene is created, every registered scene-aware subsystem gets a
/// two-pass notification (OnSceneCreated, then OnSceneReady) so they can inject their
/// per-scene systems. Scene-aware subsystems register themselves with this broker (a
/// decoupled, RTTI-free alternative to iterating + dynamic-casting the subsystem list);
/// keeping the bridge here leaves draconic.scene runtime-free.

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
    [[nodiscard]] i32 UpdateOrder() const noexcept override { return -500; }   // scenes tick early

    // ---- scene lifecycle ----

    // Creates a scene, makes it active, and notifies scene-aware subsystems (two-pass).
    Scene* CreateScene(StringView name = u8"Scene") {
        UniquePtr<Scene> owned = MakeUnique<Scene>(DefaultAllocator(), name);
        Scene* scene = owned.Get();
        m_scenes.PushBack(Move(owned));
        m_active.PushBack(scene);
        NotifyCreated(*scene);
        return scene;
    }

    // Destroys a scene. Deferred to the end of Update if called while updating.
    void DestroyScene(Scene* scene) {
        if (scene == nullptr) { return; }
        if (m_updating) { m_pendingRemove.PushBack(scene); return; }
        DestroyImmediate(scene);
    }

    [[nodiscard]] Scene* GetScene(StringView name) {
        for (Scene* s : m_active) { if (s->Name() == name) { return s; } }
        return nullptr;
    }
    [[nodiscard]] Span<Scene* const> ActiveScenes() const noexcept { return { m_active.Data(), m_active.Size() }; }

    // ---- ISceneAware broker ----

    void RegisterSceneAware(ISceneAware* aware) {
        if (aware == nullptr) { return; }
        for (ISceneAware* a : m_aware) { if (a == aware) { return; } }
        m_aware.PushBack(aware);
    }
    void UnregisterSceneAware(ISceneAware* aware) {
        for (usize i = 0; i < m_aware.Size(); ++i) {
            if (m_aware[i] == aware) { m_aware.RemoveAt(i); return; }
        }
    }

    // ---- subsystem frame phases ----

    void Update(f32 deltaTime) override {
        m_updating = true;
        for (Scene* s : m_active) { s->Update(deltaTime); }
        m_updating = false;
        ProcessPendingRemoves();
    }
    void FixedUpdate(f32 fixedDeltaTime) override {
        for (Scene* s : m_active) { s->FixedUpdate(fixedDeltaTime); }
    }

    void OnShutdown() override {
        for (usize i = m_scenes.Size(); i-- > 0;) { NotifyDestroyed(*m_scenes[i]); }
        m_active.Clear();
        m_pendingRemove.Clear();
        m_scenes.Clear();   // UniquePtr frees each Scene
    }

private:
    void NotifyCreated(Scene& scene) {
        for (ISceneAware* a : m_aware) { a->OnSceneCreated(scene); }   // pass 1: inject systems
        for (ISceneAware* a : m_aware) { a->OnSceneReady(scene); }     // pass 2: cross-subsystem safe
    }
    void NotifyDestroyed(Scene& scene) {
        for (ISceneAware* a : m_aware) { a->OnSceneDestroyed(scene); }
    }

    void DestroyImmediate(Scene* scene) {
        NotifyDestroyed(*scene);
        for (usize i = 0; i < m_active.Size(); ++i) {
            if (m_active[i] == scene) { m_active.RemoveAt(i); break; }
        }
        for (usize i = 0; i < m_scenes.Size(); ++i) {
            if (m_scenes[i].Get() == scene) { m_scenes.RemoveAt(i); break; }   // frees the Scene
        }
    }
    void ProcessPendingRemoves() {
        for (Scene* s : m_pendingRemove) { DestroyImmediate(s); }
        m_pendingRemove.Clear();
    }

    Array<UniquePtr<Scene>> m_scenes;       // ownership
    Array<Scene*>           m_active;       // active scenes (non-owning)
    Array<Scene*>           m_pendingRemove;
    Array<ISceneAware*>     m_aware;        // registered scene-aware subsystems
    bool                    m_updating = false;
};

} // namespace draconic::scene
