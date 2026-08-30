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
import engine.render;
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
                                    Float3 zoneExtents, Array<Float3>& outVertices,
                                    Array<u32>& outIndices)
    {
        outVertices.Clear();
        outIndices.Clear();
        auto* meshes = scene.GetSystem<engine::render::MeshComponentManager>();
        if (meshes == nullptr)
        {
            return 0;
        }
        // Rigid (no scale): the navmesh is baked in world units, so a zone entity's scale must
        // not warp the geometry Recast sees. A zone sharing a scaled entity with its ground would
        // otherwise un-scale that ground to unit size and erode the navmesh to nothing. The
        // runtime places the navmesh with the matching rigid frame (RuntimeZone.world).
        const Float4x4 zoneWorld = RigidPart(scene.GetWorldMatrix(zoneEntity));
        const Float4x4 zoneInv = Inverse(zoneWorld);
        const AABB zoneBox =
            AABB::FromCenterExtents(scene.GetWorldPosition(zoneEntity), zoneExtents);

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
        result.triangleCount =
            CollectNavigationGeometry(scene, zoneEntity, zone->extents, verts, indices);

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
            const Status baked = nav::NavigationMeshBuilder::Build(
                Span<const Float3>{verts.Data(), verts.Size()},
                Span<const u32>{indices.Data(), indices.Size()}, params, blob);
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
