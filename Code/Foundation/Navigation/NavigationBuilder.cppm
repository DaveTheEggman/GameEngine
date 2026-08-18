// Foundation::Navigation - :builder partition.
//
// NavigationMeshBuilder: the Recast bake. Triangle soup (positions + triangle indices, in
// whatever space the caller chose - the engine bakes in ZONE-LOCAL space) plus an agent
// profile go in; a serialized single-tile navmesh blob comes out. Pure and DETERMINISTIC:
// identical input + params produce byte-identical output (Recast has no threading or RNG in
// this path). Recast lives entirely in NavigationImpl.cpp; this interface is rc*-free.

module;
#include "Core/Prelude.h"

export module foundation.navigation:builder;

import foundation.core;

using namespace foundation::core;

export namespace foundation::navigation
{
    // The agent profile the navmesh is baked FOR (radius/height/climb are baked in, so a
    // navmesh is per-profile - the zone model relies on this). Cell size/height set the voxel
    // resolution of the bake. Defaults are Recast's "human-ish" starting point.
    struct NavigationBakeParams
    {
        f32 cellSize = 0.3f;             // xz voxel size (world units)
        f32 cellHeight = 0.2f;           // y voxel size (world units)
        f32 agentRadius = 0.6f;          // eroded off walkable edges
        f32 agentHeight = 2.0f;          // headroom needed to stand
        f32 agentMaxClimb = 0.9f;        // step/ledge the agent walks up
        f32 agentMaxSlopeDegrees = 45.0f; // steeper faces are non-walkable
    };

    class NavigationMeshBuilder
    {
    public:
        // Bake `vertices` (contiguous positions) + `indices` (3 per triangle) into `outData`
        // (a self-describing header + the Detour navmesh tile). Returns:
        //   Ok               - a navmesh was produced (outData filled)
        //   InvalidArgument  - empty / malformed input (index out of range, indices % 3 != 0)
        //   Internal         - a Recast stage failed
        //   NotFound         - the bake produced zero walkable polygons (degenerate geometry
        //                      or an agent profile that fits nowhere) - outData is left empty
        // Deterministic: same input + params -> byte-identical outData.
        [[nodiscard]] static Status Build(Span<const Float3> vertices, Span<const u32> indices,
                                          const NavigationBakeParams& params, Array<byte>& outData);
    };
}
