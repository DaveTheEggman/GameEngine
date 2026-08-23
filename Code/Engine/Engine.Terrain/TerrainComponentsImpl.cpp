// Engine::Terrain - reflection + scene-composition implementation unit.
//
// The REFLECT_VALUE body lives here (kept out of the interface; see gcc-module-interface-hygiene).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module engine.terrain;

import foundation.core;
import foundation.scene;
import foundation.terrain.resource;

using namespace foundation::core;

namespace engine::terrain
{
    REFLECT_VALUE(TerrainComponent, "rtti::engine::terrain")
    {
        builder.Attribute("displayName", String(u8"Terrain"))
            .Attribute("category", String(u8"Terrain"))
            .DataVersion(1)
            .Property<&TerrainComponent::terrain>("terrain")
            .Property<&TerrainComponent::castShadows>("castShadows")
            .Property<&TerrainComponent::visible>("visible");
    }

    void AddTerrainSceneManagers(foundation::scene::Scene& scene)
    {
        scene.AddSystem<TerrainComponentManager>();
    }

    void RegisterTerrainComponentReflection()
    {
        static const bool once = []()
        {
            RttiRegisterValue_TerrainComponent();
            return true;
        }();
        (void)once;
    }
}
