// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

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
        // TILED bake: cells per tile edge (tile world size = tileCells * cellSize).
        // 64 @ 0.3 = 19.2u tiles - small zones stay 1 tile, big ones split (and a future
        // partial rebake regenerates one tile, not the zone).
        u32 tileCells = 64;
        // Bake tiles across worker threads (tiles are independent; assembly stays row-major,
        // so the OUTPUT IS BYTE-IDENTICAL either way - the toggle trades bake latency only).
        bool parallelBake = true;
    };

    // OPT-IN bake-stage capture (Lumix-parity diagnostics): the intermediate Recast data a
    // failed or surprising bake is debugged with. Zone-local space, ready for debug draw.
    // The LIVE dtNavMesh draw stays the ground truth for what queries run on - stages show
    // how the bake ARRIVED there (where spans rasterized, where contours went).
    // A tiled blob's grid, for partial rebakes (ReadTiledBlobGrid / BuildTileInGrid).
    struct NavigationTileGridDesc
    {
        // XZ anchor the tiles align to (the ORIGINAL bake's geometry min corner). The
        // VERTICAL range is always taken from the current geometry - a rebake may add
        // taller/lower content without invalidating the grid.
        Float3 origin{0, 0, 0};
        f32 tileWorldSize = 0.0f;
        i32 countX = 0;
        i32 countY = 0;
    };

    struct NavigationBakeStages
    {
        // Simplified region contours: line-segment PAIRS (i, i+1 per segment).
        Array<Float3> contourLines;
        // Walkable span tops from the rasterized heightfield, one point per cell (strided
        // + capped - diagnostics, not a point cloud export).
        Array<Float3> walkableSamples;
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

        // TILED bake (the Lumix-parity build): the geometry's bounds split into
        // params.tileCells-sized tiles, each baked independently (row-major - deterministic)
        // into ONE multi-tile navmesh blob. Empty tiles are simply absent. Same status
        // contract as Build; NotFound = no tile produced any walkable polygon.
        [[nodiscard]] static Status BuildTiled(Span<const Float3> vertices,
                                               Span<const u32> indices,
                                               const NavigationBakeParams& params,
                                               Array<byte>& outData,
                                               NavigationBakeStages* outStages = nullptr);

        // The per-tile regeneration primitive (Lumix generateTileAt parity): bake ONE tile
        // (tx, ty of the grid BuildTiled would derive from these bounds) to raw Detour tile
        // data. BuildTiled is exactly this in a loop, so a regenerated tile is byte-identical
        // to the full bake's. NotFound = the tile holds no walkable polygons (outData empty).
        [[nodiscard]] static Status BuildTileAt(Span<const Float3> vertices,
                                                Span<const u32> indices,
                                                const NavigationBakeParams& params, i32 tileX,
                                                i32 tileY, Array<byte>& outTileData);

        // PARTIAL REBAKE support: rebuild one tile against an EXPLICIT grid (the grid a
        // previous bake's blob recorded) instead of deriving it from the current geometry
        // bounds - edited geometry may shift the bounds, and a patched tile must stay on the
        // original grid or it lands in the wrong place. A patched blob is byte-identical to
        // a FULL rebake when the geometry's VERTICAL envelope is unchanged (tile headers
        // record it); a grown envelope patches CORRECTLY but far tiles' header bytes differ.
        [[nodiscard]] static Status BuildTileInGrid(Span<const Float3> vertices,
                                                    Span<const u32> indices,
                                                    const NavigationBakeParams& params,
                                                    const NavigationTileGridDesc& grid,
                                                    i32 tileX, i32 tileY,
                                                    Array<byte>& outTileData);
    };

    /// The tile grid a v2 (tiled) blob was baked on. False for v1/invalid blobs.
    [[nodiscard]] bool ReadTiledBlobGrid(Span<const byte> blob, NavigationTileGridDesc& out);

    /// Replace tile (tileX, tileY)'s record in a v2 blob (insert when absent; EMPTY tileData
    /// removes the record). Every other tile's bytes are untouched, so a patched blob equals
    /// the full rebake of the same edited geometry byte-for-byte. False for v1/invalid blobs.
    [[nodiscard]] bool PatchTiledNavMeshBlob(Array<byte>& blob, i32 tileX, i32 tileY,
                                             Span<const byte> tileData);
}
