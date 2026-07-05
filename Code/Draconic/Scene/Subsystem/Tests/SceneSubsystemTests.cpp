// Phase 4 — the Context-level scene driver + ISceneAware injection: a scene-aware
// subsystem registers with the broker and injects a per-scene system into each new
// scene; the SceneSubsystem owns scenes, ticks them, and notifies create/ready/destroy.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.runtime;
import draconic.scene;
import draconic.scene.subsystem;

using namespace draconic::core;
using namespace draconic::scene;
namespace runtime = draconic::runtime;

namespace
{
    // A per-scene system a "render" subsystem injects into every scene.
    struct RenderSceneSystem : SceneSystem {
        int ticks = 0;
        void OnUpdate(ScenePhase p, f32) override { if (p == ScenePhase::PostTransform) { ++ticks; } }
    };

    // A Context-level subsystem that reacts to scene lifecycle (the ISceneAware role).
    class FakeRenderSubsystem : public runtime::Subsystem, public ISceneAware {
    public:
        int created = 0, ready = 0, destroyed = 0;

        void OnReady() override {
            if (SceneSubsystem* ss = GetContext()->GetSubsystem<SceneSubsystem>()) {
                ss->RegisterSceneAware(this);
            }
        }
        void OnSceneCreated(Scene& scene) override { ++created; scene.AddSystem<RenderSceneSystem>(); }
        void OnSceneReady(Scene&) override { ++ready; }
        void OnSceneDestroyed(Scene&) override { ++destroyed; }
    };
}

TEST_CASE("scene-aware subsystem injects a per-scene system on scene creation (two-pass)")
{
    runtime::Context ctx;
    SceneSubsystem* scenes = ctx.AddSubsystem<SceneSubsystem>();
    FakeRenderSubsystem* render = ctx.AddSubsystem<FakeRenderSubsystem>();
    ctx.Startup();                                   // OnReady -> render registers with the broker

    Scene* level = scenes->CreateScene(u8"level");
    REQUIRE(level != nullptr);
    CHECK(render->created == 1);
    CHECK(render->ready == 1);                        // both passes ran
    CHECK(level->GetSystem<RenderSceneSystem>() != nullptr);   // injected
    CHECK(scenes->GetScene(u8"level") == level);
    CHECK(scenes->ActiveScenes().Size() == 1);

    ctx.Shutdown();
}

TEST_CASE("the subsystem ticks its scenes each Context update")
{
    runtime::Context ctx;
    SceneSubsystem* scenes = ctx.AddSubsystem<SceneSubsystem>();
    FakeRenderSubsystem* render = ctx.AddSubsystem<FakeRenderSubsystem>();
    ctx.Startup();

    Scene* level = scenes->CreateScene();
    RenderSceneSystem* sys = level->GetSystem<RenderSceneSystem>();
    REQUIRE(sys != nullptr);

    ctx.Update(0.016f);                              // Context -> SceneSubsystem.Update -> scene.Update
    ctx.Update(0.016f);
    CHECK(sys->ticks == 2);

    ctx.Shutdown();
    (void)render;
}

TEST_CASE("destroying a scene notifies aware subsystems + drops it from the active list")
{
    runtime::Context ctx;
    SceneSubsystem* scenes = ctx.AddSubsystem<SceneSubsystem>();
    FakeRenderSubsystem* render = ctx.AddSubsystem<FakeRenderSubsystem>();
    ctx.Startup();

    Scene* a = scenes->CreateScene(u8"a");
    Scene* b = scenes->CreateScene(u8"b");
    CHECK(scenes->ActiveScenes().Size() == 2);
    CHECK(render->created == 2);

    scenes->DestroyScene(a);
    CHECK(render->destroyed == 1);
    CHECK(scenes->ActiveScenes().Size() == 1);
    CHECK(scenes->GetScene(u8"a") == nullptr);
    CHECK(scenes->GetScene(u8"b") == b);

    ctx.Shutdown();
}

TEST_CASE("scene-aware registration is idempotent; unregister stops notifications")
{
    runtime::Context ctx;
    SceneSubsystem* scenes = ctx.AddSubsystem<SceneSubsystem>();
    FakeRenderSubsystem* render = ctx.AddSubsystem<FakeRenderSubsystem>();
    ctx.Startup();

    scenes->RegisterSceneAware(render);              // duplicate (already registered in OnReady)
    scenes->CreateScene(u8"one");
    CHECK(render->created == 1);                      // notified once, not twice

    scenes->UnregisterSceneAware(render);
    scenes->CreateScene(u8"two");
    CHECK(render->created == 1);                      // no longer notified

    ctx.Shutdown();
}
