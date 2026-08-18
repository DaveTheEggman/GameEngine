// engine.navigation scene integration: a baked zone + a MoveEntity agent. At Start the subsystem
// loads the zone and registers the agent; navigate() then steers it, and the Update tick writes
// the steered position back to the entity transform until it arrives.

#include <doctest/doctest.h>
#include "Core/Prelude.h"

#include <cmath>

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.scene;
import foundation.navigation;
import foundation.navigation.resource;
import engine.navigation;

using namespace foundation::core;
using namespace foundation::navigation;
using namespace engine::navigation;
namespace scene = foundation::scene;
namespace content = foundation::content;
using foundation::resource::ResourceManager;

namespace
{
    void RemoveTree(StringView root)
    {
        foundation::vfs::NativeFileSystem fs(root);
        Array<foundation::vfs::DirEntry> entries;
        if (fs.AsEnumerable()->Enumerate(u8"", entries).IsOk())
        {
            for (const auto& e : entries)
            {
                if (!e.isDirectory)
                {
                    (void)fs.AsWritable()->Delete(e.name.AsView());
                }
            }
        }
        (void)RemoveDirectory(root);
    }

    void BakeGroundZone(Array<byte>& blob)
    {
        Array<Float3> verts;
        Array<u32> indices;
        verts.PushBack(Float3{-10, 0, -10});
        verts.PushBack(Float3{10, 0, -10});
        verts.PushBack(Float3{10, 0, 10});
        verts.PushBack(Float3{-10, 0, 10});
        const u32 t[] = {0, 3, 2, 0, 2, 1};
        for (u32 i : t)
        {
            indices.PushBack(i);
        }
        REQUIRE(NavigationMeshBuilder::Build(Span<const Float3>{verts.Data(), verts.Size()},
                                             Span<const u32>{indices.Data(), indices.Size()},
                                             NavigationBakeParams{}, blob)
                    .IsOk());
    }
}

TEST_CASE("navigation.scene: a MoveEntity agent navigates across a zone to its target")
{
    RegisterNavigationResource();
    RegisterNavigationComponentReflection();
    RemoveTree(u8"scratch_navscene_db");

    // A content DB holding the cooked zone, bound through the factory.
    foundation::vfs::NativeFileSystem mount(u8"scratch_navscene_db");
    content::ContentDatabase db(mount, BinarySerializerFactory(), u8".rasset");
    Array<byte> blob;
    BakeGroundZone(blob);
    auto* zoneInstance =
        db.RootGroup()->CreateInstance(u8"zone", NavigationZoneSource::StaticType());
    REQUIRE(zoneInstance != nullptr);
    {
        NavigationZoneSource src;
        src.navMeshBlob.Resize(blob.Size());
        MemCopy(src.navMeshBlob.Data(), blob.Data(), blob.Size());
        REQUIRE(zoneInstance->WriteObject(src).IsOk());
    }
    NavigationZoneFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    // Scene: nav managers + subsystem, a zone entity at the origin, an agent at (-5,0,0).
    scene::Scene scene(u8"nav");
    AddNavigationSceneManagers(scene);

    // The scene system carries the debug-draw settings block (physics precedent), default off.
    auto* navSystem = scene.GetSystem<NavigationSceneSystem>();
    REQUIRE(navSystem != nullptr);
    CHECK(navSystem->SettingsType() != nullptr);
    CHECK_FALSE(navSystem->Settings().debugDraw);
    navSystem->Settings().debugDraw = true; // editor/scene-settings would flip this
    CHECK(navSystem->Settings().debugDraw);

    scene::EntityHandle zoneEntity = scene.CreateEntity(u8"zone");
    NavMeshZoneComponent& zoneComp = scene.GetSystem<NavMeshZoneComponentManager>()->Add(zoneEntity);
    zoneComp.extents = Float3{15, 10, 15};
    zoneComp.zone.SetId(zoneInstance->Id());
    zoneComp.zone.Bind(manager);
    REQUIRE(zoneComp.zone.Get() != nullptr);
    REQUIRE(zoneComp.zone.Get()->IsValid());

    scene::EntityHandle agentEntity = scene.CreateEntity(u8"agent");
    scene.SetLocalPosition(agentEntity, Float3{-5, 0, 0});
    NavAgentComponent* agent = &scene.GetSystem<NavAgentComponentManager>()->Add(agentEntity);

    scene.UpdateTransforms();
    scene.Start();
    scene.SetSimulationEnabled(true);

    // Registered with the zone and given a crowd slot.
    CHECK(agent->zoneIndex >= 0);
    CHECK(agent->agentId >= 0);

    // Steer to the far side and step until arrival (12s @ 30 Hz).
    agent->navigate(5.0f, 0.0f, 0.0f);
    CHECK_FALSE(agent->finished);

    const Float3 start = scene.GetWorldPosition(agentEntity);
    for (int step = 0; step < 360 && !agent->finished; ++step)
    {
        scene.Update(1.0f / 30.0f);
    }

    const Float3 end = scene.GetWorldPosition(agentEntity);
    CHECK(agent->finished);                 // reported arrival
    CHECK(end.x > start.x + 5.0f);           // the entity actually moved across the zone (+x)
    CHECK(std::abs(end.x - 5.0f) < 1.5f);    // ...to near the target
    CHECK(agent->remaining() < 1.0f);

    scene.SetSimulationEnabled(false);
    scene.Stop();
    RemoveTree(u8"scratch_navscene_db");
}
