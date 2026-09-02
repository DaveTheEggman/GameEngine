// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Navigation - implementation unit.
//
// The bake logic + its heavy imports (engine.render for MeshComponent, geometry for StaticMesh,
// navigation.pipeline for the asset write) live HERE, out of the interface (GCC gcm-cluster
// hygiene). See NavigationBake.cppm for the surface.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module editor.navigation;

import foundation.core;
import foundation.scene;
import foundation.content;
import foundation.geometry;
import foundation.heightfield;
import foundation.terrain.resource;
import engine.render;
import engine.terrain;
import engine.navigation;
import foundation.navigation;
import foundation.navigation.resource; // kNavigationZoneFrameRigid (the bake stamp)
import navigation.pipeline;

using namespace foundation::core;

namespace editor::navigation
{
    namespace nav = foundation::navigation;
    namespace geometry = foundation::geometry;

    namespace
    {
        // World-space AABB of a local-space AABB under a transform (all 8 corners).
        [[nodiscard]] AABB WorldBounds(const AABB& local, const Float4x4& world)
        {
            AABB out = AABB::Empty();
            for (int i = 0; i < 8; ++i)
            {
                const Float3 corner{(i & 1) ? local.max.x : local.min.x,
                                    (i & 2) ? local.max.y : local.min.y,
                                    (i & 4) ? local.max.z : local.min.z};
                out.Expand(TransformPoint(corner, world));
            }
            return out;
        }
    }

    usize CollectNavigationGeometry(scene::Scene& scene, scene::EntityHandle zoneEntity,
                                    Float3 zoneExtents, f32 cellSize, Array<Float3>& outVertices,
                                    Array<u32>& outIndices)
    {
        outVertices.Clear();
        outIndices.Clear();
        // Rigid (no scale): the navmesh is baked in world units, so a zone entity's scale must
        // not warp the geometry Recast sees. A zone sharing a scaled entity with its ground would
        // otherwise un-scale that ground to unit size and erode the navmesh to nothing. The
        // runtime places the navmesh with the matching rigid frame (RuntimeZone.world).
        const Float4x4 zoneWorld = RigidPart(scene.GetWorldMatrix(zoneEntity));
        const Float4x4 zoneInv = Inverse(zoneWorld);
        const AABB zoneBox =
            AABB::FromCenterExtents(scene.GetWorldPosition(zoneEntity), zoneExtents);

        auto* meshes = scene.GetSystem<engine::render::MeshComponentManager>();
        if (meshes != nullptr)
        meshes->ForEach(
            [&](engine::render::MeshComponent& c, scene::EntityHandle entity)
            {
                geometry::StaticMesh* mesh = c.mesh.Get();
                if (mesh == nullptr || mesh->VertexCount() == 0 || mesh->IndexCount() == 0)
                {
                    return;
                }
                const Float4x4 meshWorld = scene.GetWorldMatrix(entity);
                if (!WorldBounds(mesh->bounds, meshWorld).Intersects(zoneBox))
                {
                    return;
                }
                const u32 base = static_cast<u32>(outVertices.Size());
                for (const geometry::StaticMeshVertex& v : mesh->vertices)
                {
                    const Float3 world = TransformPoint(v.position, meshWorld);
                    outVertices.PushBack(TransformPoint(world, zoneInv)); // -> zone-local
                }
                const u32 indexCount = mesh->IndexCount();
                for (u32 i = 0; i < indexCount; ++i)
                {
                    outIndices.PushBack(base + mesh->indices.Get(i));
                }
            });

        // Terrain: triangulate the shared heightfield surface (the same grid physics collides
        // against) inside the zone box, so agents can walk on terrain. Sampled no finer than
        // the zone's cell size - Recast re-voxelizes anyway.
        auto* terrains = scene.GetSystem<engine::terrain::TerrainComponentManager>();
        if (terrains != nullptr)
        {
            terrains->ForEach(
                [&](engine::terrain::TerrainComponent& c, scene::EntityHandle entity)
                {
                    foundation::terrain::TerrainResource* terrain = c.terrain.Get();
                    foundation::heightfield::Heightfield* field =
                        (terrain != nullptr) ? terrain->heightfield.Get() : nullptr;
                    if (field == nullptr || field->Size() < 2)
                    {
                        return;
                    }
                    const Float4x4 terrainWorld = scene.GetWorldMatrix(entity);
                    const Float2 footprint = field->WorldSize();
                    const AABB localBox{
                        Float3{-footprint.x * 0.5f, field->MinY(), -footprint.y * 0.5f},
                        Float3{footprint.x * 0.5f, field->MaxY(), footprint.y * 0.5f}};
                    if (!WorldBounds(localBox, terrainWorld).Intersects(zoneBox))
                    {
                        return;
                    }

                    // The zone box in terrain-local space bounds the grid range to triangulate.
                    const AABB zoneLocal = WorldBounds(zoneBox, Inverse(terrainWorld));
                    const i32 last = field->Size() - 1;
                    const Float2 g0 = field->WorldToGrid(zoneLocal.min.x, zoneLocal.min.z);
                    const Float2 g1 = field->WorldToGrid(zoneLocal.max.x, zoneLocal.max.z);
                    const i32 x0 = Clamp(static_cast<i32>(Floor(g0.x)), 0, last);
                    const i32 z0 = Clamp(static_cast<i32>(Floor(g0.y)), 0, last);
                    const i32 x1 = Clamp(static_cast<i32>(Ceil(g1.x)), 0, last);
                    const i32 z1 = Clamp(static_cast<i32>(Ceil(g1.y)), 0, last);
                    if (x1 <= x0 || z1 <= z0)
                    {
                        return;
                    }

                    const f32 spacing = footprint.x / static_cast<f32>(last);
                    const i32 stride =
                        Max(1, static_cast<i32>(cellSize / Max(spacing, 0.0001f)));

                    // Sample coordinates along each axis (stride steps, last row/column always
                    // included so the surface reaches the zone edge).
                    Array<i32> xs;
                    Array<i32> zs;
                    for (i32 gx = x0; gx < x1; gx += stride)
                    {
                        xs.PushBack(gx);
                    }
                    xs.PushBack(x1);
                    for (i32 gz = z0; gz < z1; gz += stride)
                    {
                        zs.PushBack(gz);
                    }
                    zs.PushBack(z1);

                    const u32 base = static_cast<u32>(outVertices.Size());
                    for (i32 gz : zs)
                    {
                        for (i32 gx : xs)
                        {
                            const Float2 xz =
                                field->GridToWorld(static_cast<f32>(gx), static_cast<f32>(gz));
                            const Float3 local{xz.x, field->GetHeightAtGrid(gx, gz), xz.y};
                            const Float3 world = TransformPoint(local, terrainWorld);
                            outVertices.PushBack(TransformPoint(world, zoneInv));
                        }
                    }
                    const u32 columns = static_cast<u32>(xs.Size());
                    for (u32 row = 0; row + 1 < static_cast<u32>(zs.Size()); ++row)
                    {
                        for (u32 col = 0; col + 1 < columns; ++col)
                        {
                            const u32 v00 = base + row * columns + col;
                            const u32 v10 = v00 + 1;
                            const u32 v01 = v00 + columns;
                            const u32 v11 = v01 + 1;
                            // +Y face normals (Recast's walkable filter keys on them).
                            outIndices.PushBack(v00);
                            outIndices.PushBack(v01);
                            outIndices.PushBack(v11);
                            outIndices.PushBack(v00);
                            outIndices.PushBack(v11);
                            outIndices.PushBack(v10);
                        }
                    }
                });
        }
        return outIndices.Size() / 3u;
    }

    BakeResult BakeNavigationZone(scene::Scene& scene, scene::EntityHandle zoneEntity,
                                  foundation::content::Instance& targetAsset)
    {
        BakeResult result;
        auto* zones = scene.GetSystem<engine::navigation::NavMeshZoneComponentManager>();
        if (zones == nullptr)
        {
            return result;
        }
        engine::navigation::NavMeshZoneComponent* zone = zones->Get(zoneEntity);
        if (zone == nullptr)
        {
            return result;
        }

        Array<Float3> verts;
        Array<u32> indices;
        result.triangleCount = CollectNavigationGeometry(scene, zoneEntity, zone->extents,
                                                         zone->cellSize, verts, indices);

        pipeline::NavigationZoneAsset asset;
        if (result.triangleCount > 0)
        {
            nav::NavigationBakeParams params;
            params.cellSize = zone->cellSize;
            params.cellHeight = zone->cellHeight;
            params.agentRadius = zone->agentRadius;
            params.agentHeight = zone->agentHeight;
            params.agentMaxClimb = zone->agentMaxClimb;
            params.agentMaxSlopeDegrees = zone->agentMaxSlopeDegrees;

            Array<byte> blob;
            // TILED (the Lumix-parity build): small zones come out as one tile; large ones
            // split, and the per-tile primitive can regenerate a single tile later. v1
            // single-tile blobs still load (the reader sniffs the version). Stages are
            // captured every editor bake and parked on the scene system - the
            // debugDrawBakeStages overlay shows how THIS bake arrived at its mesh.
            nav::NavigationBakeStages stages;
            const Status baked = nav::NavigationMeshBuilder::BuildTiled(
                Span<const Float3>{verts.Data(), verts.Size()},
                Span<const u32>{indices.Data(), indices.Size()}, params, blob, &stages);
            if (auto* system = scene.GetSystem<engine::navigation::NavigationSceneSystem>())
            {
                system->SetBakeStages(
                    zoneEntity, static_cast<Array<Float3>&&>(stages.contourLines),
                    static_cast<Array<Float3>&&>(stages.walkableSamples));
            }
            if (baked.IsOk() && !blob.IsEmpty())
            {
                asset.navMeshBlob.Resize(blob.Size());
                MemCopy(asset.navMeshBlob.Data(), blob.Data(), blob.Size());
                asset.bakedFrame = nav::kNavigationZoneFrameRigid;
                result.baked = true;
            }
        }

        if (!pipeline::WriteNavigationZoneAsset(targetAsset, asset).IsOk())
        {
            LOG_ERROR(u8"Editor", u8"navigation bake: writing the zone asset failed");
            result.baked = false;
        }
        return result;
    }
}
