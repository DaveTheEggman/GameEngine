// Engine::Terrain - the `engine.terrain` module.
//
// PHASE A (the authoring surface): TerrainComponent references a cooked Terrain resource (heightfield
// + splatmap + layers, all resolved through the manager) and carries a per-instance cast-shadows +
// visible flag. The chunked geo-mipmap RENDERER (the reason engine.terrain owns terrain) lands in a
// later phase - it consumes this component via the dynamic-category renderer dispatch (sprites/
// particles precedent), so Engine.Render stays terrain-free. Physics is the SEPARATE
// ShapeKind::Heightfield collider referencing the same heightfield - terrain does not add collision.

module;
#include "Core/Prelude.h"

export module engine.terrain;

export import :heighttexture; // Phase B: the GPU height-texture cache

import foundation.core;
import foundation.scene;
import foundation.resource;
import foundation.terrain.resource;

using namespace foundation::core;

export namespace engine::terrain
{
    using foundation::terrain::TerrainResource;

    struct TerrainComponent
    {
        // The cooked terrain product (Ref<TerrainResource>): heightfield + splatmap + layers. Bound through
        // the manager; the heightfield inside is the SHARED source of truth with the physics collider.
        foundation::resource::Ref<TerrainResource> terrain;
        bool castShadows = true; // this instance casts into the CSM (terrain default-on)
        bool visible = true;
    };

    inline void Serialize(ISerializer& ar, TerrainComponent& c)
    {
        foundation::core::Serialize(ar, "terrain", c.terrain);
        foundation::core::Serialize(ar, "castShadows", c.castShadows);
        foundation::core::Serialize(ar, "visible", c.visible);
    }

    inline void ResolveResources(foundation::resource::ResourceManager& manager, TerrainComponent& c)
    {
        c.terrain.Bind(manager);
    }

    class TerrainComponentManager final
        : public foundation::scene::SerializableComponentManager<TerrainComponent>
    {
    public:
        TerrainComponentManager()
            : SerializableComponentManager<TerrainComponent>(u8"terrain")
        {
        }
    };

    // Scene-composition hooks (the SceneModule pair, mirroring every other domain).
    void AddTerrainSceneManagers(foundation::scene::Scene& scene);
    void RegisterTerrainComponentReflection();
}
