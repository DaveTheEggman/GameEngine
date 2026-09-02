// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Foundation::Navigation - implementation unit.
//
// ALL Recast/Detour contact lives here: the interface stays rc*/dt*-free (API hygiene + GCC's
// module serializer cannot digest the third-party headers inside an interface unit's global
// fragment). The bake is the canonical Recast "solo mesh" pipeline; the runtime wrappers hold
// Detour objects behind PIMPL.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Profiler/Profiler.h" // PROFILE_SCOPE (compiles to nothing when disabled)

#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourCrowd.h>

#include <cmath>
#include <cstring>

module foundation.navigation;

import foundation.core;
import foundation.profiler;

using namespace foundation::core;

namespace foundation::navigation
{
    namespace
    {
        // Serialized-blob header. The Detour tile data follows immediately after.
        constexpr u32 kBlobMagic = 0x564E4144u; // 'DANV' little-endian
        constexpr u32 kBlobVersion = 1u;

        struct BlobHeader
        {
            u32 magic;
            u32 version;
            f32 agentRadius;
            f32 agentHeight;
            u32 navDataSize;
        };

        // Detour poly flag for the one walkable class we bake. Non-zero so the default query
        // filter (include 0xffff) accepts it.
        constexpr unsigned short kPolyFlagWalk = 0x01;
    }

    // ---------------------------------------------------------------------------------------
    // Bake (Recast)
    // ---------------------------------------------------------------------------------------

    Status NavigationMeshBuilder::Build(Span<const Float3> vertices, Span<const u32> indices,
                                        const NavigationBakeParams& params, Array<byte>& outData)
    {
        PROFILE_SCOPE("Navigation.Bake"); // measures the bake (the async-deferral trigger, >100ms)
        outData.Clear();

        if (vertices.IsEmpty() || indices.IsEmpty() || (indices.Size() % 3u) != 0u)
        {
            return Status{ErrorCode::InvalidArgument};
        }
        const int vertexCount = static_cast<int>(vertices.Size());
        const int triangleCount = static_cast<int>(indices.Size() / 3u);

        // u32 -> int triangle list (Recast wants int); bounds-check every index.
        Array<int> tris;
        tris.Resize(indices.Size());
        for (usize i = 0; i < indices.Size(); ++i)
        {
            if (indices[i] >= vertices.Size())
            {
                return Status{ErrorCode::InvalidArgument};
            }
            tris[i] = static_cast<int>(indices[i]);
        }
        // Float3 is three contiguous f32s, so the span doubles as Recast's float* vertex array.
        const float* verts = reinterpret_cast<const float*>(vertices.Data());

        float bmin[3] = {verts[0], verts[1], verts[2]};
        float bmax[3] = {verts[0], verts[1], verts[2]};
        for (int i = 1; i < vertexCount; ++i)
        {
            for (int c = 0; c < 3; ++c)
            {
                bmin[c] = rcMin(bmin[c], verts[i * 3 + c]);
                bmax[c] = rcMax(bmax[c], verts[i * 3 + c]);
            }
        }

        rcConfig cfg;
        std::memset(&cfg, 0, sizeof(cfg));
        cfg.cs = params.cellSize;
        cfg.ch = params.cellHeight;
        cfg.walkableSlopeAngle = params.agentMaxSlopeDegrees;
        cfg.walkableHeight = static_cast<int>(std::ceil(params.agentHeight / cfg.ch));
        cfg.walkableClimb = static_cast<int>(std::floor(params.agentMaxClimb / cfg.ch));
        cfg.walkableRadius = static_cast<int>(std::ceil(params.agentRadius / cfg.cs));
        cfg.maxEdgeLen = static_cast<int>(12.0f / cfg.cs);
        cfg.maxSimplificationError = 1.3f;
        cfg.minRegionArea = static_cast<int>(rcSqr(8));   // rm small isolated regions
        cfg.mergeRegionArea = static_cast<int>(rcSqr(20)); // merge small into neighbours
        cfg.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
        cfg.detailSampleDist = cfg.cs * 6.0f;
        cfg.detailSampleMaxError = cfg.ch * 1.0f;
        rcVcopy(cfg.bmin, bmin);
        rcVcopy(cfg.bmax, bmax);
        rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

        rcContext ctx(false); // no timers, no logging

        rcHeightfield* solid = nullptr;
        rcCompactHeightfield* chf = nullptr;
        rcContourSet* cset = nullptr;
        rcPolyMesh* pmesh = nullptr;
        rcPolyMeshDetail* dmesh = nullptr;

        // The build runs inside a lambda so a single cleanup block frees every Recast object on
        // any exit. `outData` is filled before the cleanup.
        auto run = [&]() -> Status
        {
            solid = rcAllocHeightfield();
            if (solid == nullptr ||
                !rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax,
                                     cfg.cs, cfg.ch))
            {
                return Status{ErrorCode::Internal};
            }

            Array<unsigned char> triAreas;
            triAreas.Resize(static_cast<usize>(triangleCount));
            std::memset(triAreas.Data(), 0, triAreas.Size());
            rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts, vertexCount, tris.Data(),
                                    triangleCount, triAreas.Data());
            if (!rcRasterizeTriangles(&ctx, verts, vertexCount, tris.Data(), triAreas.Data(),
                                      triangleCount, *solid, cfg.walkableClimb))
            {
                return Status{ErrorCode::Internal};
            }

            rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
            rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
            rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);

            chf = rcAllocCompactHeightfield();
            if (chf == nullptr || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight,
                                                             cfg.walkableClimb, *solid, *chf))
            {
                return Status{ErrorCode::Internal};
            }
            if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf))
            {
                return Status{ErrorCode::Internal};
            }
            // Watershed partitioning (deterministic; the default for good-quality solo meshes).
            if (!rcBuildDistanceField(&ctx, *chf) ||
                !rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea))
            {
                return Status{ErrorCode::Internal};
            }

            cset = rcAllocContourSet();
            if (cset == nullptr ||
                !rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset))
            {
                return Status{ErrorCode::Internal};
            }

            pmesh = rcAllocPolyMesh();
            if (pmesh == nullptr || !rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh))
            {
                return Status{ErrorCode::Internal};
            }

            dmesh = rcAllocPolyMeshDetail();
            if (dmesh == nullptr || !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist,
                                                           cfg.detailSampleMaxError, *dmesh))
            {
                return Status{ErrorCode::Internal};
            }

            if (pmesh->npolys == 0)
            {
                return Status{ErrorCode::NotFound}; // nothing walkable came out
            }

            // Flag every walkable poly so the default Detour filter accepts it.
            for (int i = 0; i < pmesh->npolys; ++i)
            {
                if (pmesh->areas[i] == RC_WALKABLE_AREA)
                {
                    pmesh->flags[i] = kPolyFlagWalk;
                }
            }

            dtNavMeshCreateParams np;
            std::memset(&np, 0, sizeof(np));
            np.verts = pmesh->verts;
            np.vertCount = pmesh->nverts;
            np.polys = pmesh->polys;
            np.polyAreas = pmesh->areas;
            np.polyFlags = pmesh->flags;
            np.polyCount = pmesh->npolys;
            np.nvp = pmesh->nvp;
            np.detailMeshes = dmesh->meshes;
            np.detailVerts = dmesh->verts;
            np.detailVertsCount = dmesh->nverts;
            np.detailTris = dmesh->tris;
            np.detailTriCount = dmesh->ntris;
            np.walkableHeight = params.agentHeight;
            np.walkableRadius = params.agentRadius;
            np.walkableClimb = params.agentMaxClimb;
            rcVcopy(np.bmin, pmesh->bmin);
            rcVcopy(np.bmax, pmesh->bmax);
            np.cs = cfg.cs;
            np.ch = cfg.ch;
            np.buildBvTree = true;

            unsigned char* navData = nullptr;
            int navDataSize = 0;
            if (!dtCreateNavMeshData(&np, &navData, &navDataSize) || navData == nullptr ||
                navDataSize <= 0)
            {
                return Status{ErrorCode::Internal};
            }

            BlobHeader header;
            header.magic = kBlobMagic;
            header.version = kBlobVersion;
            header.agentRadius = params.agentRadius;
            header.agentHeight = params.agentHeight;
            header.navDataSize = static_cast<u32>(navDataSize);

            outData.Resize(sizeof(BlobHeader) + static_cast<usize>(navDataSize));
            std::memcpy(outData.Data(), &header, sizeof(BlobHeader));
            std::memcpy(outData.Data() + sizeof(BlobHeader), navData,
                        static_cast<usize>(navDataSize));
            dtFree(navData);
            return Status{};
        };

        const Status status = run();

        rcFreePolyMeshDetail(dmesh);
        rcFreePolyMesh(pmesh);
        rcFreeContourSet(cset);
        rcFreeCompactHeightfield(chf);
        rcFreeHeightField(solid);

        if (!status.IsOk())
        {
            outData.Clear();
        }
        return status;
    }

    // ---------------------------------------------------------------------------------------
    // NavigationMesh
    // ---------------------------------------------------------------------------------------

    struct NavigationMesh::Impl
    {
        dtNavMesh* navMesh = nullptr;
        f32 agentRadius = 0.0f;
        f32 agentHeight = 0.0f;

        ~Impl()
        {
            if (navMesh != nullptr)
            {
                dtFreeNavMesh(navMesh); // frees the DT_TILE_FREE_DATA-owned tile too
            }
        }
    };

    NavigationMesh::NavigationMesh() : m_impl(MakeUnique<Impl>(DefaultAllocator())) {}
    NavigationMesh::~NavigationMesh() = default;
    NavigationMesh::NavigationMesh(NavigationMesh&&) noexcept = default;
    NavigationMesh& NavigationMesh::operator=(NavigationMesh&&) noexcept = default;

    Status NavigationMesh::Load(Span<const byte> data)
    {
        Impl& impl = *m_impl;
        if (impl.navMesh != nullptr)
        {
            dtFreeNavMesh(impl.navMesh);
            impl.navMesh = nullptr;
            impl.agentRadius = 0.0f;
            impl.agentHeight = 0.0f;
        }

        if (data.Size() < sizeof(BlobHeader))
        {
            return Status{ErrorCode::InvalidArgument};
        }
        BlobHeader header;
        std::memcpy(&header, data.Data(), sizeof(BlobHeader));
        if (header.magic != kBlobMagic || header.version != kBlobVersion)
        {
            return Status{ErrorCode::InvalidArgument};
        }
        if (data.Size() != sizeof(BlobHeader) + static_cast<usize>(header.navDataSize))
        {
            return Status{ErrorCode::InvalidArgument};
        }

        // Detour needs the tile data in a dtAlloc'd buffer it can own+free (DT_TILE_FREE_DATA).
        unsigned char* tile =
            static_cast<unsigned char*>(dtAlloc(header.navDataSize, DT_ALLOC_PERM));
        if (tile == nullptr)
        {
            return Status{ErrorCode::OutOfMemory};
        }
        std::memcpy(tile, data.Data() + sizeof(BlobHeader), header.navDataSize);

        dtNavMesh* mesh = dtAllocNavMesh();
        if (mesh == nullptr)
        {
            dtFree(tile);
            return Status{ErrorCode::OutOfMemory};
        }
        const dtStatus st =
            mesh->init(tile, static_cast<int>(header.navDataSize), DT_TILE_FREE_DATA);
        if (dtStatusFailed(st))
        {
            dtFreeNavMesh(mesh); // frees `tile` (ownership was handed over)
            return Status{ErrorCode::Internal};
        }

        impl.navMesh = mesh;
        impl.agentRadius = header.agentRadius;
        impl.agentHeight = header.agentHeight;
        return Status{};
    }

    bool NavigationMesh::IsValid() const noexcept { return m_impl->navMesh != nullptr; }

    void NavigationMesh::DebugTriangles(Array<Float3>& out) const
    {
        const dtNavMesh* mesh = m_impl->navMesh;
        if (mesh == nullptr)
        {
            return;
        }
        for (int t = 0; t < mesh->getMaxTiles(); ++t)
        {
            const dtMeshTile* tile = mesh->getTile(t);
            if (tile == nullptr || tile->header == nullptr)
            {
                continue;
            }
            for (int i = 0; i < tile->header->polyCount; ++i)
            {
                const dtPoly* poly = &tile->polys[i];
                if (poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
                {
                    continue; // off-mesh links are lines, not surface
                }
                const dtPolyDetail* detail = &tile->detailMeshes[i];
                for (int j = 0; j < detail->triCount; ++j)
                {
                    const unsigned char* tri = &tile->detailTris[(detail->triBase + j) * 4];
                    for (int k = 0; k < 3; ++k)
                    {
                        const float* v;
                        if (tri[k] < poly->vertCount)
                        {
                            v = &tile->verts[poly->verts[tri[k]] * 3];
                        }
                        else
                        {
                            v = &tile->detailVerts[(detail->vertBase + tri[k] - poly->vertCount) *
                                                   3];
                        }
                        out.PushBack(Float3{v[0], v[1], v[2]});
                    }
                }
            }
        }
    }

    f32 NavigationMesh::BakedAgentRadius() const noexcept { return m_impl->agentRadius; }
    f32 NavigationMesh::BakedAgentHeight() const noexcept { return m_impl->agentHeight; }
    void* NavigationMesh::NativeHandle() const noexcept { return m_impl->navMesh; }

    // ---------------------------------------------------------------------------------------
    // NavigationMeshQuery
    // ---------------------------------------------------------------------------------------

    namespace
    {
        constexpr int kMaxPathPolys = 256;
        constexpr int kMaxStraightPath = 256;
        // Search box half-size for snapping a world point onto the navmesh.
        constexpr float kQueryHalfExtents[3] = {2.0f, 4.0f, 2.0f};
    }

    struct NavigationMeshQuery::Impl
    {
        dtNavMeshQuery* query = nullptr;
        dtQueryFilter filter; // default: include 0xffff, unit area costs

        ~Impl()
        {
            if (query != nullptr)
            {
                dtFreeNavMeshQuery(query);
            }
        }
    };

    NavigationMeshQuery::NavigationMeshQuery(const NavigationMesh& mesh)
        : m_impl(MakeUnique<Impl>(DefaultAllocator()))
    {
        dtNavMesh* navMesh = static_cast<dtNavMesh*>(mesh.NativeHandle());
        if (navMesh == nullptr)
        {
            return;
        }
        dtNavMeshQuery* query = dtAllocNavMeshQuery();
        if (query == nullptr)
        {
            return;
        }
        if (dtStatusFailed(query->init(navMesh, 2048)))
        {
            dtFreeNavMeshQuery(query);
            return;
        }
        m_impl->query = query;
    }

    NavigationMeshQuery::~NavigationMeshQuery() = default;
    NavigationMeshQuery::NavigationMeshQuery(NavigationMeshQuery&&) noexcept = default;
    NavigationMeshQuery& NavigationMeshQuery::operator=(NavigationMeshQuery&&) noexcept = default;

    bool NavigationMeshQuery::IsValid() const noexcept { return m_impl->query != nullptr; }

    bool NavigationMeshQuery::FindNearestPoint(Float3 point, Float3& out) const
    {
        if (m_impl->query == nullptr)
        {
            return false;
        }
        const float p[3] = {point.x, point.y, point.z};
        dtPolyRef ref = 0;
        float nearest[3] = {0, 0, 0};
        const dtStatus st =
            m_impl->query->findNearestPoly(p, kQueryHalfExtents, &m_impl->filter, &ref, nearest);
        if (dtStatusFailed(st) || ref == 0)
        {
            return false;
        }
        out = Float3{nearest[0], nearest[1], nearest[2]};
        return true;
    }

    Status NavigationMeshQuery::FindPath(Float3 start, Float3 end, NavigationPath& out) const
    {
        out.corners.Clear();
        out.complete = false;
        if (m_impl->query == nullptr)
        {
            return Status{ErrorCode::Internal};
        }

        const float startPos[3] = {start.x, start.y, start.z};
        const float endPos[3] = {end.x, end.y, end.z};
        dtPolyRef startRef = 0, endRef = 0;
        float startNearest[3], endNearest[3];
        const dtStatus s1 = m_impl->query->findNearestPoly(startPos, kQueryHalfExtents,
                                                           &m_impl->filter, &startRef, startNearest);
        const dtStatus s2 = m_impl->query->findNearestPoly(endPos, kQueryHalfExtents,
                                                           &m_impl->filter, &endRef, endNearest);
        if (dtStatusFailed(s1) || dtStatusFailed(s2) || startRef == 0 || endRef == 0)
        {
            return Status{ErrorCode::NotFound}; // start or end is off-mesh
        }

        dtPolyRef pathPolys[kMaxPathPolys];
        int pathCount = 0;
        const dtStatus fp = m_impl->query->findPath(startRef, endRef, startNearest, endNearest,
                                                    &m_impl->filter, pathPolys, &pathCount,
                                                    kMaxPathPolys);
        if (dtStatusFailed(fp) || pathCount == 0)
        {
            return Status{ErrorCode::NotFound};
        }

        // Reachable only if the path actually ends on the destination polygon. Detour also flags
        // DT_PARTIAL_RESULT; check both.
        const bool partial =
            dtStatusDetail(fp, DT_PARTIAL_RESULT) || pathPolys[pathCount - 1] != endRef;

        float straight[kMaxStraightPath * 3];
        unsigned char straightFlags[kMaxStraightPath];
        dtPolyRef straightRefs[kMaxStraightPath];
        int straightCount = 0;
        const dtStatus sp = m_impl->query->findStraightPath(startNearest, endNearest, pathPolys,
                                                            pathCount, straight, straightFlags,
                                                            straightRefs, &straightCount,
                                                            kMaxStraightPath);
        if (dtStatusFailed(sp))
        {
            return Status{ErrorCode::Internal};
        }

        out.corners.Reserve(static_cast<usize>(straightCount));
        for (int i = 0; i < straightCount; ++i)
        {
            out.corners.PushBack(
                Float3{straight[i * 3 + 0], straight[i * 3 + 1], straight[i * 3 + 2]});
        }
        out.complete = !partial;
        return Status{};
    }

    // ---------------------------------------------------------------------------------------
    // NavigationCrowd
    // ---------------------------------------------------------------------------------------

    struct NavigationCrowd::Impl
    {
        dtCrowd* crowd = nullptr;

        ~Impl()
        {
            if (crowd != nullptr)
            {
                dtFreeCrowd(crowd);
            }
        }
    };

    NavigationCrowd::NavigationCrowd(const NavigationMesh& mesh, i32 maxAgents, f32 maxAgentRadius)
        : m_impl(MakeUnique<Impl>(DefaultAllocator()))
    {
        dtNavMesh* navMesh = static_cast<dtNavMesh*>(mesh.NativeHandle());
        if (navMesh == nullptr || maxAgents <= 0)
        {
            return;
        }
        dtCrowd* crowd = dtAllocCrowd();
        if (crowd == nullptr)
        {
            return;
        }
        if (!crowd->init(maxAgents, maxAgentRadius > 0.0f ? maxAgentRadius : 0.6f, navMesh))
        {
            dtFreeCrowd(crowd);
            return;
        }
        m_impl->crowd = crowd;
    }

    NavigationCrowd::~NavigationCrowd() = default;
    NavigationCrowd::NavigationCrowd(NavigationCrowd&&) noexcept = default;
    NavigationCrowd& NavigationCrowd::operator=(NavigationCrowd&&) noexcept = default;

    bool NavigationCrowd::IsValid() const noexcept { return m_impl->crowd != nullptr; }

    i32 NavigationCrowd::AddAgent(Float3 position, const NavigationAgentParams& params)
    {
        if (m_impl->crowd == nullptr)
        {
            return -1;
        }
        dtCrowdAgentParams ap;
        std::memset(&ap, 0, sizeof(ap));
        ap.radius = params.radius;
        ap.height = params.height;
        ap.maxSpeed = params.maxSpeed;
        ap.maxAcceleration = params.maxAcceleration;
        ap.collisionQueryRange = params.radius * 12.0f;
        ap.pathOptimizationRange = params.radius * 30.0f;
        ap.separationWeight = 2.0f;
        ap.obstacleAvoidanceType = 3;
        ap.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS |
                         DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION;
        const float pos[3] = {position.x, position.y, position.z};
        return m_impl->crowd->addAgent(pos, &ap);
    }

    NavigationAgentState NavigationCrowd::AgentState(i32 agentId) const
    {
        NavigationAgentState out;
        if (m_impl->crowd == nullptr || agentId < 0)
        {
            return out;
        }
        const dtCrowdAgent* agent = m_impl->crowd->getAgent(agentId);
        if (agent == nullptr || !agent->active)
        {
            return out;
        }
        out.valid = true;
        switch (agent->state)
        {
        case DT_CROWDAGENT_STATE_WALKING: out.state = NavAgentCrowdState::Walking; break;
        case DT_CROWDAGENT_STATE_OFFMESH: out.state = NavAgentCrowdState::OffMesh; break;
        default: out.state = NavAgentCrowdState::Invalid; break;
        }
        switch (agent->targetState)
        {
        case DT_CROWDAGENT_TARGET_NONE: out.targetState = NavAgentTargetState::None; break;
        case DT_CROWDAGENT_TARGET_VALID: out.targetState = NavAgentTargetState::Valid; break;
        case DT_CROWDAGENT_TARGET_VELOCITY:
            out.targetState = NavAgentTargetState::Velocity;
            break;
        case DT_CROWDAGENT_TARGET_FAILED: out.targetState = NavAgentTargetState::Failed; break;
        default: // REQUESTING / WAITING_FOR_QUEUE / WAITING_FOR_PATH - all "in flight"
            out.targetState = NavAgentTargetState::Requesting;
            break;
        }
        out.desiredSpeed = agent->desiredSpeed;
        out.cornerCount = agent->ncorners;
        return out;
    }

    void NavigationCrowd::SetAgentParams(i32 agentId, const NavigationAgentParams& params)
    {
        if (m_impl->crowd == nullptr || agentId < 0)
        {
            return;
        }
        const dtCrowdAgent* agent = m_impl->crowd->getAgent(agentId);
        if (agent == nullptr || !agent->active)
        {
            return;
        }
        dtCrowdAgentParams ap = agent->params; // keep the flags/avoidance the add configured
        ap.radius = params.radius;
        ap.height = params.height;
        ap.maxSpeed = params.maxSpeed;
        ap.maxAcceleration = params.maxAcceleration;
        ap.collisionQueryRange = params.radius * 12.0f;
        ap.pathOptimizationRange = params.radius * 30.0f;
        m_impl->crowd->updateAgentParameters(agentId, &ap);
    }

    void NavigationCrowd::RemoveAgent(i32 agentId)
    {
        if (m_impl->crowd != nullptr && agentId >= 0)
        {
            m_impl->crowd->removeAgent(agentId);
        }
    }

    bool NavigationCrowd::SetTarget(i32 agentId, Float3 target)
    {
        if (m_impl->crowd == nullptr || agentId < 0)
        {
            return false;
        }
        const dtNavMeshQuery* query = m_impl->crowd->getNavMeshQuery();
        const dtQueryFilter* filter = m_impl->crowd->getFilter(0);
        const float* ext = m_impl->crowd->getQueryExtents();
        const float t[3] = {target.x, target.y, target.z};
        dtPolyRef ref = 0;
        float nearest[3] = {0, 0, 0};
        if (dtStatusFailed(query->findNearestPoly(t, ext, filter, &ref, nearest)) || ref == 0)
        {
            return false;
        }
        return m_impl->crowd->requestMoveTarget(agentId, ref, nearest);
    }

    void NavigationCrowd::ClearTarget(i32 agentId)
    {
        if (m_impl->crowd != nullptr && agentId >= 0)
        {
            m_impl->crowd->resetMoveTarget(agentId);
        }
    }

    void NavigationCrowd::Update(f32 deltaTime)
    {
        if (m_impl->crowd != nullptr && deltaTime > 0.0f)
        {
            m_impl->crowd->update(deltaTime, nullptr);
        }
    }

    Float3 NavigationCrowd::AgentPosition(i32 agentId) const
    {
        if (m_impl->crowd == nullptr || agentId < 0)
        {
            return Float3{0, 0, 0};
        }
        const dtCrowdAgent* agent = m_impl->crowd->getAgent(agentId);
        if (agent == nullptr || !agent->active)
        {
            return Float3{0, 0, 0};
        }
        return Float3{agent->npos[0], agent->npos[1], agent->npos[2]};
    }

    Float3 NavigationCrowd::AgentVelocity(i32 agentId) const
    {
        if (m_impl->crowd == nullptr || agentId < 0)
        {
            return Float3{0, 0, 0};
        }
        const dtCrowdAgent* agent = m_impl->crowd->getAgent(agentId);
        if (agent == nullptr || !agent->active)
        {
            return Float3{0, 0, 0};
        }
        return Float3{agent->vel[0], agent->vel[1], agent->vel[2]};
    }

    bool NavigationCrowd::IsAgentValid(i32 agentId) const
    {
        if (m_impl->crowd == nullptr || agentId < 0)
        {
            return false;
        }
        const dtCrowdAgent* agent = m_impl->crowd->getAgent(agentId);
        return agent != nullptr && agent->active;
    }
}
