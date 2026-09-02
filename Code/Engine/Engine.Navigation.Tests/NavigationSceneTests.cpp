// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

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

TEST_CASE("navigation.scene: a SCALED zone entity places the navmesh rigidly (no double-scale)")
{
    // The bake and the runtime both use the scale-free RigidPart frame, so a zone on a
    // scaled entity behaves exactly like the unscaled one - the navmesh's world-unit geometry is
    // PLACED, never warped. The rigid-baked stamp (bakedFrame) marks the artifact.
    RegisterNavigationResource();
    RegisterNavigationComponentReflection();
    RemoveTree(u8"scratch_navscene_scaled_db");

    foundation::vfs::NativeFileSystem mount(u8"scratch_navscene_scaled_db");
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
        src.bakedFrame = kNavigationZoneFrameRigid; // what NavigationBakeImpl stamps
        REQUIRE(zoneInstance->WriteObject(src).IsOk());
    }
    NavigationZoneFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    scene::Scene scene(u8"nav-scaled");
    AddNavigationSceneManagers(scene);

    scene::EntityHandle zoneEntity = scene.CreateEntity(u8"zone");
    {
        Transform t;
        t.scale = Float3{2.0f, 2.0f, 2.0f}; // the desync trigger before the rigid frame
        scene.SetLocalTransform(zoneEntity, t);
    }
    NavMeshZoneComponent& zoneComp = scene.GetSystem<NavMeshZoneComponentManager>()->Add(zoneEntity);
    zoneComp.extents = Float3{15, 10, 15};
    zoneComp.zone.SetId(zoneInstance->Id());
    zoneComp.zone.Bind(manager);
    REQUIRE(zoneComp.zone.Get() != nullptr);
    REQUIRE(zoneComp.zone.Get()->IsValid());
    CHECK(zoneComp.zone.Get()->bakedFrame == kNavigationZoneFrameRigid);

    scene::EntityHandle agentEntity = scene.CreateEntity(u8"agent");
    scene.SetLocalPosition(agentEntity, Float3{-5, 0, 0});
    NavAgentComponent* agent = &scene.GetSystem<NavAgentComponentManager>()->Add(agentEntity);

    scene.UpdateTransforms();
    scene.Start();
    scene.SetSimulationEnabled(true);
    REQUIRE(agent->zoneIndex >= 0); // the scaled zone still loaded (rigid frame)

    agent->navigate(5.0f, 0.0f, 0.0f);
    for (int step = 0; step < 360 && !agent->finished; ++step)
    {
        scene.Update(1.0f / 30.0f);
    }
    const Float3 end = scene.GetWorldPosition(agentEntity);
    CHECK(agent->finished);
    CHECK(std::abs(end.x - 5.0f) < 1.5f); // same arrival as the unscaled zone - no double-scale
    CHECK(std::abs(end.y) < 0.5f);        // ...and ON the ground plane, not floated/sunk

    scene.SetSimulationEnabled(false);
    scene.Stop();
    RemoveTree(u8"scratch_navscene_scaled_db");
}

TEST_CASE("navigation.scene: a LEGACY bake on a scaled zone entity is skipped, on unit scale loads")
{
    // A pre-rigid-frame bake (bakedFrame 0) on a SCALED entity would desync silently
    // (agents path off the floor); the subsystem now refuses it with a warning. On a unit-scale
    // entity the two conventions agree exactly, so legacy zones keep working.
    RegisterNavigationResource();
    RegisterNavigationComponentReflection();
    RemoveTree(u8"scratch_navscene_legacy_db");

    foundation::vfs::NativeFileSystem mount(u8"scratch_navscene_legacy_db");
    content::ContentDatabase db(mount, BinarySerializerFactory(), u8".rasset");
    Array<byte> blob;
    BakeGroundZone(blob);
    auto* zoneInstance =
        db.RootGroup()->CreateInstance(u8"zone", NavigationZoneSource::StaticType());
    REQUIRE(zoneInstance != nullptr);
    {
        NavigationZoneSource src; // bakedFrame stays 0 = legacy
        src.navMeshBlob.Resize(blob.Size());
        MemCopy(src.navMeshBlob.Data(), blob.Data(), blob.Size());
        REQUIRE(zoneInstance->WriteObject(src).IsOk());
    }
    NavigationZoneFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    // Scaled entity + legacy bake -> the zone is refused (agent gets no slot).
    {
        scene::Scene scene(u8"nav-legacy-scaled");
        AddNavigationSceneManagers(scene);
        scene::EntityHandle zoneEntity = scene.CreateEntity(u8"zone");
        Transform t;
        t.scale = Float3{2.0f, 2.0f, 2.0f};
        scene.SetLocalTransform(zoneEntity, t);
        NavMeshZoneComponent& zc = scene.GetSystem<NavMeshZoneComponentManager>()->Add(zoneEntity);
        zc.extents = Float3{15, 10, 15};
        zc.zone.SetId(zoneInstance->Id());
        zc.zone.Bind(manager);
        REQUIRE(zc.zone.Get() != nullptr);
        scene::EntityHandle agentEntity = scene.CreateEntity(u8"agent");
        scene.SetLocalPosition(agentEntity, Float3{-5, 0, 0});
        NavAgentComponent* agent = &scene.GetSystem<NavAgentComponentManager>()->Add(agentEntity);
        scene.UpdateTransforms();
        scene.Start();
        scene.SetSimulationEnabled(true);
        CHECK(agent->zoneIndex < 0); // refused, not silently desynced
        scene.SetSimulationEnabled(false);
        scene.Stop();
    }

    // Unit-scale entity + legacy bake -> loads and navigates (backwards compatible).
    {
        scene::Scene scene(u8"nav-legacy-unit");
        AddNavigationSceneManagers(scene);
        scene::EntityHandle zoneEntity = scene.CreateEntity(u8"zone");
        NavMeshZoneComponent& zc = scene.GetSystem<NavMeshZoneComponentManager>()->Add(zoneEntity);
        zc.extents = Float3{15, 10, 15};
        zc.zone.SetId(zoneInstance->Id());
        zc.zone.Bind(manager);
        REQUIRE(zc.zone.Get() != nullptr);
        scene::EntityHandle agentEntity = scene.CreateEntity(u8"agent");
        scene.SetLocalPosition(agentEntity, Float3{-5, 0, 0});
        NavAgentComponent* agent = &scene.GetSystem<NavAgentComponentManager>()->Add(agentEntity);
        scene.UpdateTransforms();
        scene.Start();
        scene.SetSimulationEnabled(true);
        CHECK(agent->zoneIndex >= 0); // legacy + unit scale = fine
        scene.SetSimulationEnabled(false);
        scene.Stop();
    }

    RemoveTree(u8"scratch_navscene_legacy_db");
}

TEST_CASE("navigation.scene: per-agent speed applies live and stopDistance arrives short")
{
    RegisterNavigationResource();
    RegisterNavigationComponentReflection();
    RemoveTree(u8"scratch_navspeed_db");

    foundation::vfs::NativeFileSystem mount(u8"scratch_navspeed_db");
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

    scene::Scene scene(u8"nav");
    AddNavigationSceneManagers(scene);
    scene::EntityHandle zoneEntity = scene.CreateEntity(u8"zone");
    NavMeshZoneComponent& zoneComp =
        scene.GetSystem<NavMeshZoneComponentManager>()->Add(zoneEntity);
    zoneComp.extents = Float3{15, 10, 15};
    zoneComp.zone.SetId(zoneInstance->Id());
    zoneComp.zone.Bind(manager);
    REQUIRE(zoneComp.zone.Get() != nullptr);

    // SLOW agent: a lower per-call speed covers less ground in the same steps.
    scene::EntityHandle slowEntity = scene.CreateEntity(u8"slow");
    scene.SetLocalPosition(slowEntity, Float3{-5, 0, -2});
    NavAgentComponent* slow = &scene.GetSystem<NavAgentComponentManager>()->Add(slowEntity);

    // ARRIVE agent: navigateAt with a stop distance parks on the ring, not the point.
    scene::EntityHandle arriveEntity = scene.CreateEntity(u8"arrive");
    scene.SetLocalPosition(arriveEntity, Float3{-5, 0, 2});
    NavAgentComponent* arrive = &scene.GetSystem<NavAgentComponentManager>()->Add(arriveEntity);

    scene.UpdateTransforms();
    scene.Start();
    scene.SetSimulationEnabled(true);
    REQUIRE(slow->agentId >= 0);
    REQUIRE(arrive->agentId >= 0);

    slow->setSpeed(0.8f); // live change - the tick pushes it into the crowd
    slow->navigate(5.0f, 0.0f, -2.0f);
    arrive->navigateAt(5.0f, 0.0f, 2.0f, 3.5f, 2.5f);

    for (int step = 0; step < 90; ++step) // 3 seconds
    {
        scene.Update(1.0f / 30.0f);
    }

    // 3s at 0.8 u/s cannot cross 10 units; the default 3.5 u/s agent with a 2.5 stop ring
    // has already parked.
    const Float3 slowPos = scene.GetWorldPosition(slowEntity);
    CHECK_FALSE(slow->finished);
    CHECK(slowPos.x < 0.0f);        // well short of the target
    CHECK(slowPos.x > -4.5f);       // but moving

    CHECK(arrive->finished);
    const Float3 arrivePos = scene.GetWorldPosition(arriveEntity);
    const f32 dx = arrivePos.x - 5.0f;
    const f32 dz = arrivePos.z - 2.0f;
    const f32 distance = Sqrt(dx * dx + dz * dz);
    CHECK(distance > 1.2f); // parked on the ring, NOT on the point
    CHECK(distance < 3.5f);
    CHECK(arrive->remaining() > 1.2f);

    // Introspection (Lumix parity): the still-moving agent reads as WALKING on a VALID
    // move request with a live speed intent; the parked one has released its target.
    CHECK(slow->state() == 1);       // NavAgentCrowdState::Walking
    CHECK(slow->targetState() == 2); // NavAgentTargetState::Valid
    CHECK(slow->desiredSpeed() > 0.0f);
    CHECK(slow->desiredSpeed() < 1.0f); // capped by the per-call speed
    CHECK(arrive->state() == 1);
    CHECK(arrive->targetState() == 0); // arrival ClearTarget -> None

    // Raising the slow agent's speed mid-run applies live: it now finishes the crossing.
    slow->setSpeed(6.0f);
    for (int step = 0; step < 240 && !slow->finished; ++step)
    {
        scene.Update(1.0f / 30.0f);
    }
    CHECK(slow->finished);

    scene.SetSimulationEnabled(false);
    scene.Stop();
    RemoveTree(u8"scratch_navspeed_db");
}
