// Foundation::Navigation - :mesh partition.
//
// The Detour runtime wrappers: NavigationMesh (a loaded single-tile navmesh), its query
// (NavigationMeshQuery - pathfinding) and its crowd (NavigationCrowd - agents that steer to
// targets and avoid each other). Each is PIMPL: the Detour dt* types live only in
// NavigationImpl.cpp. A query/crowd borrows its mesh's Detour handle, so the NavigationMesh
// MUST outlive every query/crowd built against it (the engine subsystem owns all three per
// zone, so this holds naturally).

module;
#include "Core/Prelude.h"

export module foundation.navigation:mesh;

import foundation.core;

using namespace foundation::core;

export namespace foundation::navigation
{
    // A straight-line path across the navmesh (corner points from start to end). `complete`
    // is false when the destination could not be reached - `corners` then leads to the nearest
    // reachable point. An empty `corners` with a failing Status means start/end were off-mesh.
    struct NavigationPath
    {
        Array<Float3> corners;
        bool complete = false;
    };

    // Per-agent steering profile for the crowd.
    struct NavigationAgentParams
    {
        f32 radius = 0.6f;
        f32 height = 2.0f;
        f32 maxSpeed = 3.5f;
        f32 maxAcceleration = 8.0f;
    };

    // A loaded single-tile navmesh (from NavigationMeshBuilder::Build output). Move-only; owns
    // the Detour navmesh.
    class NavigationMesh
    {
    public:
        NavigationMesh();
        ~NavigationMesh();
        NavigationMesh(NavigationMesh&&) noexcept;
        NavigationMesh& operator=(NavigationMesh&&) noexcept;
        NavigationMesh(const NavigationMesh&) = delete;
        NavigationMesh& operator=(const NavigationMesh&) = delete;

        // Load a serialized navmesh blob (Build output). Replaces any prior contents.
        [[nodiscard]] Status Load(Span<const byte> data);
        [[nodiscard]] bool IsValid() const noexcept;

        // The agent radius/height the mesh was baked for (recovered from the blob header). Zero
        // when invalid. The crowd uses the radius to size local avoidance.
        [[nodiscard]] f32 BakedAgentRadius() const noexcept;
        [[nodiscard]] f32 BakedAgentHeight() const noexcept;

        // INTERNAL: the Detour dtNavMesh* as an opaque handle, for query/crowd construction
        // inside foundation.navigation only. Null when invalid. Do not reinterpret elsewhere.
        [[nodiscard]] void* NativeHandle() const noexcept;

    private:
        struct Impl;
        UniquePtr<Impl> m_impl;
    };

    // Pathfinding against a NavigationMesh. Move-only. The mesh must outlive the query.
    class NavigationMeshQuery
    {
    public:
        explicit NavigationMeshQuery(const NavigationMesh& mesh);
        ~NavigationMeshQuery();
        NavigationMeshQuery(NavigationMeshQuery&&) noexcept;
        NavigationMeshQuery& operator=(NavigationMeshQuery&&) noexcept;
        NavigationMeshQuery(const NavigationMeshQuery&) = delete;
        NavigationMeshQuery& operator=(const NavigationMeshQuery&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;

        // Path from `start` to `end`. Ok + out.complete=true when reachable; Ok +
        // out.complete=false when the nearest reachable point falls short (unreachable
        // destination - reported, never a crash or a false success); a failing Status when
        // start or end has no polygon within the search box.
        [[nodiscard]] Status FindPath(Float3 start, Float3 end, NavigationPath& out) const;

        // Nearest point ON the navmesh to `point` within the default search box. False if none.
        [[nodiscard]] bool FindNearestPoint(Float3 point, Float3& out) const;

    private:
        struct Impl;
        UniquePtr<Impl> m_impl;
    };

    // A Detour crowd bound to one NavigationMesh: agents steer toward per-agent targets and
    // avoid each other. Move-only. The mesh must outlive the crowd. One crowd per navmesh
    // (Detour builds its internal query against a single mesh).
    class NavigationCrowd
    {
    public:
        NavigationCrowd(const NavigationMesh& mesh, i32 maxAgents, f32 maxAgentRadius);
        ~NavigationCrowd();
        NavigationCrowd(NavigationCrowd&&) noexcept;
        NavigationCrowd& operator=(NavigationCrowd&&) noexcept;
        NavigationCrowd(const NavigationCrowd&) = delete;
        NavigationCrowd& operator=(const NavigationCrowd&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;

        // Add an agent at `position` (snapped to the nearest polygon). Returns an agent id
        // (>= 0) or -1 on failure (crowd full, off-mesh spawn, invalid crowd).
        [[nodiscard]] i32 AddAgent(Float3 position, const NavigationAgentParams& params);
        void RemoveAgent(i32 agentId);

        // Steer the agent toward `target` (snapped to the nearest polygon). False if the agent
        // is invalid or the target has no polygon within the search box.
        [[nodiscard]] bool SetTarget(i32 agentId, Float3 target);
        void ClearTarget(i32 agentId);

        void Update(f32 deltaTime);

        [[nodiscard]] Float3 AgentPosition(i32 agentId) const;
        [[nodiscard]] Float3 AgentVelocity(i32 agentId) const;
        [[nodiscard]] bool IsAgentValid(i32 agentId) const;

    private:
        struct Impl;
        UniquePtr<Impl> m_impl;
    };
}
