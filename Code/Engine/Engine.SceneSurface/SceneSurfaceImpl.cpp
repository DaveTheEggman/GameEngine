// Engine::SceneSurface - implementation unit (the wide subsystem imports live here, keeping the
// interface BMI lean). One call per domain; the domain functions are the SAME ones the subsystems'
// OnSceneCreated delegate to, so this aggregate cannot drift from the runtime injection.

module;
#include "Core/Prelude.h"

module engine.scenesurface;

import foundation.core;
import foundation.scene;
import foundation.net.replication;
import engine.render;
import engine.animation;
import engine.particles;
import engine.physics;
import engine.audio;
import engine.script;
import engine.ui;

using namespace foundation::core;

namespace engine
{
    void AddAllSceneManagers(foundation::scene::Scene& scene)
    {
        render::AddRenderSceneManagers(scene);        // 9 (incl. Environment + PostProcess settings)
        animation::AddAnimationSceneManagers(scene);  // 4 (incl. PropertyAnimator)
        particles::AddParticleSceneManagers(scene);   // 1
        physics::AddPhysicsSceneManagers(scene);      // 5 (incl. the physics settings system)
        audio::AddAudioSceneManagers(scene);          // 4 (engine-less AudioSceneSystem)
        script::AddScriptSceneManagers(scene);        // 3 (host-less script systems)
        ui::AddUISceneManagers(scene);                // 3
        foundation::net::AddNetworkSceneManagers(scene); // 2
    }

    void RegisterAllSceneComponentReflection()
    {
        render::RegisterRenderComponentReflection();
        animation::RegisterAnimationComponentReflection();
        particles::RegisterParticleComponentReflection();
        physics::RegisterPhysicsComponentReflection();
        audio::RegisterAudioComponentReflection();
        script::RegisterScriptComponentReflection();
        ui::RegisterUIComponentReflection();
        foundation::net::RegisterReplicationComponents();
    }
}
