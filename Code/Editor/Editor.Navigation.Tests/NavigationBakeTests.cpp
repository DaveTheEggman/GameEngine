// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor navigation bake: a scene with a ground mesh + a zone -> collect the in-zone geometry in
// zone-local space -> Recast bake -> write the NavigationZoneAsset sidecar. The written blob loads
// back into a NavigationMesh and paths, proving the whole author-side chain end to end (headless).

#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.scene;
import foundation.geometry;
import engine.render;
import engine.navigation;
import engine.terrain;
import foundation.terrain.resource;
import foundation.heightfield;
import foundation.navigation;
import foundation.navigation.resource;
import navigation.pipeline;
import editor.navigation;

using namespace foundation::core;
namespace scene = foundation::scene;
namespace geometry = foundation::geometry;
namespace content = foundation::content;
using namespace foundation::navigation;

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

    RefPtr<geometry::StaticMesh> GroundMesh()
    {
        RefPtr<geometry::StaticMesh> mesh = MakeRef<geometry::StaticMesh>(DefaultAllocator());
        const Float3 corners[] = {{-10, 0, -10}, {10, 0, -10}, {10, 0, 10}, {-10, 0, 10}};
        for (const Float3& p : corners)
        {
            geometry::StaticMeshVertex v;
            v.position = p;
            v.normal = Float3{0, 1, 0};
            mesh->vertices.PushBack(v);
        }
        const u32 tris[] = {0, 3, 2, 0, 2, 1}; // +Y winding
        mesh->indices.Resize(sizeof(tris) / sizeof(tris[0])); // Add writes into a sized buffer
        for (u32 i : tris)
        {
            mesh->indices.Add(i);
        }
        mesh->CalculateBounds();
        return mesh;
    }

    // A UNIT (1x1) ground quad centered at the origin. Meant to be scaled up by its entity - the
    // case where the zone shares that scaled entity, so the bake frame must strip the scale.
    RefPtr<geometry::StaticMesh> UnitGroundMesh()
    {
        RefPtr<geometry::StaticMesh> mesh = MakeRef<geometry::StaticMesh>(DefaultAllocator());
        const Float3 corners[] = {{-0.5f, 0, -0.5f}, {0.5f, 0, -0.5f}, {0.5f, 0, 0.5f},
                                  {-0.5f, 0, 0.5f}};
        for (const Float3& p : corners)
        {
            geometry::StaticMeshVertex v;
            v.position = p;
            v.normal = Float3{0, 1, 0};
            mesh->vertices.PushBack(v);
        }
        const u32 tris[] = {0, 3, 2, 0, 2, 1}; // +Y winding
        mesh->indices.Resize(sizeof(tris) / sizeof(tris[0]));
        for (u32 i : tris)
        {
            mesh->indices.Add(i);
        }
        mesh->CalculateBounds();
        return mesh;
    }

    // An in-memory terrain over a 65x65 heightfield (the smallest legal grid): heights come
    // from `heightAt(gx, gz)` in world Y, quantized onto [minY, maxY].
    RefPtr<foundation::terrain::TerrainResource> MakeTerrain(f32 minY, f32 maxY, f32 worldSide,
                                                             f32 (*heightAt)(i32, i32))
    {
        constexpr i32 kSide = 65;
        RefPtr<foundation::heightfield::Heightfield> field =
            MakeRef<foundation::heightfield::Heightfield>(
                DefaultAllocator(), kSide, Float2{worldSide, worldSide}, minY, maxY);
        for (i32 gz = 0; gz < kSide; ++gz)
        {
            for (i32 gx = 0; gx < kSide; ++gx)
            {
                field->SetSample(gx, gz, field->WorldYToSample(heightAt(gx, gz)));
            }
        }
        RefPtr<foundation::terrain::TerrainResource> terrain =
            MakeRef<foundation::terrain::TerrainResource>(DefaultAllocator());
        terrain->heightfield = field;
        return terrain;
    }

    // Scene + zone + one terrain entity; returns the baked navmesh blob (empty = bake failed).
    Array<byte> BakeTerrainZone(RefPtr<foundation::terrain::TerrainResource> terrain,
                                Float3 zoneExtents, usize& outTriangles)
    {
        scene::Scene sceneObj(u8"bake_terrain");
        engine::navigation::AddNavigationSceneManagers(sceneObj);
        auto* terrains = sceneObj.AddSystem<engine::terrain::TerrainComponentManager>();

        scene::EntityHandle terrainEntity = sceneObj.CreateEntity(u8"terrain");
        engine::terrain::TerrainComponent& tc = terrains->Add(terrainEntity);
        tc.terrain = foundation::resource::Ref<foundation::terrain::TerrainResource>(terrain);

        scene::EntityHandle zoneEntity = sceneObj.CreateEntity(u8"zone");
        engine::navigation::NavMeshZoneComponent& z =
            sceneObj.GetSystem<engine::navigation::NavMeshZoneComponentManager>()->Add(zoneEntity);
        z.extents = zoneExtents;
        sceneObj.UpdateTransforms();

        Array<Float3> verts;
        Array<u32> indices;
        outTriangles = editor::navigation::CollectNavigationGeometry(
            sceneObj, zoneEntity, z.extents, z.cellSize, verts, indices);

        Array<byte> blob;
        if (outTriangles > 0)
        {
            NavigationBakeParams params;
            params.cellSize = z.cellSize;
            params.cellHeight = z.cellHeight;
            params.agentRadius = z.agentRadius;
            params.agentHeight = z.agentHeight;
            params.agentMaxClimb = z.agentMaxClimb;
            params.agentMaxSlopeDegrees = z.agentMaxSlopeDegrees;
            (void)NavigationMeshBuilder::Build(Span<const Float3>{verts.Data(), verts.Size()},
                                               Span<const u32>{indices.Data(), indices.Size()},
                                               params, blob);
        }
        return blob;
    }

    bool PathAcross(Span<const byte> blob, Float3 from, Float3 to)
    {
        NavigationMesh mesh;
        if (!mesh.Load(blob).IsOk() || !mesh.IsValid())
        {
            return false;
        }
        NavigationMeshQuery query(mesh);
        NavigationPath path;
        return query.FindPath(from, to, path).IsOk() && path.complete;
    }
}

TEST_CASE("editor.navigation: bake collects scene geometry and writes a loadable zone asset")
{
    RegisterNavigationResource();
    pipeline::RegisterNavigationZoneAsset();
    RemoveTree(u8"scratch_navbake_db");

    foundation::vfs::NativeFileSystem mount(u8"scratch_navbake_db");
    content::ContentDatabase db(mount, BinarySerializerFactory(), u8".rasset");
    auto* assetInstance =
        db.RootGroup()->CreateInstance(u8"zone", pipeline::NavigationZoneAsset::StaticType());
    REQUIRE(assetInstance != nullptr);

    // Scene: a ground mesh at the origin + a zone entity that covers it.
    scene::Scene sceneObj(u8"bake");
    engine::navigation::AddNavigationSceneManagers(sceneObj);
    auto* meshes = sceneObj.AddSystem<engine::render::MeshComponentManager>();

    RefPtr<geometry::StaticMesh> ground = GroundMesh();
    scene::EntityHandle groundEntity = sceneObj.CreateEntity(u8"ground");
    engine::render::MeshComponent& mc = meshes->Add(groundEntity);
    mc.mesh = foundation::resource::Ref<geometry::StaticMesh>(ground);

    scene::EntityHandle zoneEntity = sceneObj.CreateEntity(u8"zone");
    engine::navigation::NavMeshZoneComponent& z =
        sceneObj.GetSystem<engine::navigation::NavMeshZoneComponentManager>()->Add(zoneEntity);
    z.extents = Float3{15, 10, 15};

    sceneObj.UpdateTransforms();

    // Bake: collect the ground triangles (2) and write the navmesh into the asset sidecar.
    const editor::navigation::BakeResult result =
        editor::navigation::BakeNavigationZone(sceneObj, zoneEntity, *assetInstance);
    CHECK(result.triangleCount == 2u);
    CHECK(result.baked);

    // The written asset carries a navmesh (in the sidecar) that loads and paths.
    pipeline::NavigationZoneAsset readBack;
    {
        RefPtr<ISerializable> object = assetInstance->ReadObject();
        auto* asset = Cast<pipeline::NavigationZoneAsset>(object.Get());
        REQUIRE(asset != nullptr);
        REQUIRE(pipeline::EnsureNavMeshLoaded(*assetInstance, *asset).IsOk());
        REQUIRE(!asset->navMeshBlob.IsEmpty());
        readBack.navMeshBlob = asset->navMeshBlob;
    }
    NavigationMesh mesh;
    REQUIRE(mesh.Load(Span<const byte>{reinterpret_cast<const byte*>(readBack.navMeshBlob.Data()),
                                       readBack.navMeshBlob.Size()})
                .IsOk());
    REQUIRE(mesh.IsValid());
    NavigationMeshQuery query(mesh);
    NavigationPath path;
    REQUIRE(query.FindPath(Float3{-8, 0, 0}, Float3{8, 0, 0}, path).IsOk());
    CHECK(path.complete);

    RemoveTree(u8"scratch_navbake_db");
}

TEST_CASE("editor.navigation: bake succeeds when the zone shares a SCALED entity with its ground")
{
    // Regression: authoring a nav zone directly on the ground entity, where that entity is scaled
    // up (e.g. a unit plane scaled to 20x20). The bake frame must be rigid - otherwise
    // Inverse(zoneWorld) cancels the entity's scale, hands Recast a 1x1 plane, and the agent-radius
    // erosion wipes it ("No walkable geometry inside the zone"). With RigidPart the geometry keeps
    // its true world size and bakes.
    pipeline::RegisterNavigationZoneAsset();
    RemoveTree(u8"scratch_navbake_scaled_db");

    foundation::vfs::NativeFileSystem mount(u8"scratch_navbake_scaled_db");
    content::ContentDatabase db(mount, BinarySerializerFactory(), u8".rasset");
    auto* assetInstance =
        db.RootGroup()->CreateInstance(u8"zone", pipeline::NavigationZoneAsset::StaticType());
    REQUIRE(assetInstance != nullptr);

    scene::Scene sceneObj(u8"bake_scaled");
    engine::navigation::AddNavigationSceneManagers(sceneObj);
    auto* meshes = sceneObj.AddSystem<engine::render::MeshComponentManager>();

    // ONE entity carries both the (unit) ground mesh and the zone, scaled up 20x in X/Z.
    RefPtr<geometry::StaticMesh> ground = UnitGroundMesh();
    scene::EntityHandle entity = sceneObj.CreateEntity(u8"ground_zone");
    engine::render::MeshComponent& mc = meshes->Add(entity);
    mc.mesh = foundation::resource::Ref<geometry::StaticMesh>(ground);

    engine::navigation::NavMeshZoneComponent& z =
        sceneObj.GetSystem<engine::navigation::NavMeshZoneComponentManager>()->Add(entity);
    z.extents = Float3{15, 10, 15};

    Transform t = sceneObj.GetLocalTransform(entity);
    t.scale = Float3{20, 1, 20};
    sceneObj.SetLocalTransform(entity, t);
    sceneObj.UpdateTransforms();

    const editor::navigation::BakeResult result =
        editor::navigation::BakeNavigationZone(sceneObj, entity, *assetInstance);
    CHECK(result.triangleCount == 2u); // the unit plane's world bounds (20x20) intersect the zone
    CHECK(result.baked);               // fails without RigidPart: the unit-size plane erodes away

    RemoveTree(u8"scratch_navbake_scaled_db");
}

TEST_CASE("editor.navigation: terrain contributes walkable surface to the bake")
{
    usize triangles = 0;
    Array<byte> blob = BakeTerrainZone(
        MakeTerrain(0.0f, 10.0f, 32.0f, [](i32, i32) { return 2.0f; }), Float3{14, 8, 14},
        triangles);
    CHECK(triangles > 0);
    REQUIRE(!blob.IsEmpty());
    CHECK(PathAcross(Span<const byte>(blob.Data(), blob.Size()), Float3{-10, 2, 0},
                     Float3{10, 2, 0}));
}

TEST_CASE("editor.navigation: an agent paths across sloped terrain")
{
    // A gentle ramp: ~4 units of rise over the 32-unit footprint (~7 degrees).
    usize triangles = 0;
    Array<byte> blob = BakeTerrainZone(
        MakeTerrain(0.0f, 10.0f, 32.0f,
                    [](i32 gx, i32) { return static_cast<f32>(gx) * (4.0f / 64.0f); }),
        Float3{14, 8, 14}, triangles);
    REQUIRE(!blob.IsEmpty());
    CHECK(PathAcross(Span<const byte>(blob.Data(), blob.Size()), Float3{-10, 1, 0},
                     Float3{10, 3, 0}));
}

TEST_CASE("editor.navigation: a too-steep terrain wall splits the navmesh")
{
    // A cliff across the middle: ~24 units of rise over one cell (~88 degrees) - far past the
    // walkable-slope filter, so the two plateaus must not connect.
    usize triangles = 0;
    Array<byte> blob = BakeTerrainZone(
        MakeTerrain(0.0f, 30.0f, 32.0f,
                    [](i32 gx, i32) { return gx < 32 ? 1.0f : 25.0f; }),
        Float3{14, 28, 14}, triangles);
    REQUIRE(!blob.IsEmpty());
    CHECK(!PathAcross(Span<const byte>(blob.Data(), blob.Size()), Float3{-10, 1, 0},
                      Float3{10, 25, 0}));
    // Each plateau itself remains walkable.
    CHECK(PathAcross(Span<const byte>(blob.Data(), blob.Size()), Float3{-10, 1, 0},
                     Float3{-3, 1, 6}));
}
