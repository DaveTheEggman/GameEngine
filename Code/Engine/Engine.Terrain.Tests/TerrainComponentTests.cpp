// engine.terrain (Phase A): the TerrainComponent's reflected surface + a scene serialize round-trip
// (the terrain reference + flags survive save/load through the component manager).
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.scene;
import foundation.scene.resource; // SerializeScene
import foundation.resource;
import foundation.terrain.resource;
import engine.terrain;

using namespace foundation::core;
using namespace engine::terrain;
namespace scene = foundation::scene;

TEST_CASE("engine.terrain: TerrainComponent reflects its authored fields")
{
    RegisterTerrainComponentReflection();
    const TypeInfo& type = TypeOf<TerrainComponent>();
    CHECK(FindProperty(type, "terrain") != nullptr);
    CHECK(FindProperty(type, "castShadows") != nullptr);
    CHECK(FindProperty(type, "visible") != nullptr);
}

TEST_CASE("engine.terrain: a TerrainComponent survives a scene serialize round-trip")
{
    RegisterTerrainComponentReflection();

    scene::Scene a{u8"terrain-wire"};
    a.AddSystem<TerrainComponentManager>();
    scene::EntityHandle e = a.CreateEntity(u8"terrain");
    {
        TerrainComponent& t = a.GetSystem<TerrainComponentManager>()->Add(e);
        t.terrain.SetId(Guid(42, 7));
        t.castShadows = false;
        t.visible = false;
    }

    MemoryStream stream;
    {
        BinarySerializer writer(stream, SerializeMode::Write);
        scene::SerializeScene(writer, a);
    }
    (void)stream.Seek(0, SeekOrigin::Begin);

    scene::Scene b{u8"terrain-wire2"};
    b.AddSystem<TerrainComponentManager>();
    {
        BinarySerializer reader(stream, SerializeMode::Read);
        scene::SerializeScene(reader, b);
    }

    scene::EntityHandle loaded = b.FindEntity(a.GetEntityId(e));
    REQUIRE(loaded.IsAssigned());
    TerrainComponent* t = b.GetSystem<TerrainComponentManager>()->Get(loaded);
    REQUIRE(t != nullptr);
    CHECK(t->terrain.id == Guid(42, 7));
    CHECK(t->castShadows == false);
    CHECK(t->visible == false);
}
