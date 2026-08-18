// Engine::ScriptSurface - implementation unit.
//
// The wide fan-in over every subsystem that contributes a script facade lives HERE (one TU),
// keeping the interface BMI lean for the hosts. This is the ONE place the full subsystem-facade
// set is written down; a surface-describing host calls RegisterAllScriptFacades, never the ten
// registrars by hand.

module;
#include "Core/Prelude.h"

module engine.scriptsurface;

import foundation.core;
import foundation.script.facades; // RegisterScriptFacadeReflection (base Entity/Log/Time/Random)
import engine.physics;
import engine.navigation;
import engine.audio;
import engine.input;
import engine.ui;
import engine.render;
import engine.particles;
import engine.animation;
import engine.gameinstance;
import foundation.net.manager;
import foundation.net.replication;

using namespace foundation::core;

namespace engine
{
    void RegisterAllScriptFacades()
    {
        // Base surface first: core reflected types + the built-in behavior facades. Idempotent, so
        // a host that already did these (script_api does) pays nothing.
        RegisterCoreTypes();
        foundation::script::RegisterScriptFacadeReflection();

        // Every subsystem's metadata-only facade registrar (no device/world/GPU touched).
        engine::physics::RegisterPhysicsScriptFacade();
        engine::navigation::RegisterNavigationScriptFacade();
        engine::audio::RegisterAudioScriptFacade();
        engine::input::RegisterInputScriptFacade();
        engine::ui::RegisterUiScriptFacade();
        engine::render::RegisterRenderScriptFacade();
        engine::particles::RegisterParticleScriptFacade();
        engine::animation::RegisterAnimationScriptFacade();
        engine::runtime::RegisterSceneLoaderScriptFacade();
        foundation::net::RegisterNetScriptFacade();
        foundation::net::RegisterNetworkComponentScriptFacade();
    }
}
