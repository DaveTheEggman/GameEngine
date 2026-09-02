// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// foundation.navigation battery. Everything here is HEADLESS: the target links only
// Foundation::Navigation (Recast/Detour + Core), no render/physics/device. The bake is a pure
// function; the runtime wrappers need only a serialized blob.
//
// Coverage:
//   - bake determinism: same soup + params -> byte-identical; params change -> data change
//   - query: a path across a fixture with a central box detours AROUND it; a disconnected
//     destination is reported (complete=false), an off-mesh destination fails (not a crash)
//   - crowd: two agents crossing head-on both arrive, positions advance, no NaN, no hard overlap

#include <doctest/doctest.h>
#include "Core/Prelude.h"

#include <algorithm> // std::min / std::max - libstdc++ leaks these in, the MSVC STL does not
#include <cmath>
#include <cstring>

import foundation.core;
import foundation.navigation;

using namespace foundation::core;
using namespace foundation::navigation;

namespace
{
    // Ground quad on y=0 spanning [minX,maxX] x [minZ,maxZ], wound so both triangles face +Y
    // (Recast marks only up-facing triangles walkable).
    void AddGround(Array<Float3>& verts, Array<u32>& indices, f32 minX, f32 maxX, f32 minZ,
                   f32 maxZ)
    {
        const u32 base = static_cast<u32>(verts.Size());
        verts.PushBack(Float3{minX, 0.0f, minZ}); // 0
        verts.PushBack(Float3{maxX, 0.0f, minZ}); // 1
        verts.PushBack(Float3{maxX, 0.0f, maxZ}); // 2
        verts.PushBack(Float3{minX, 0.0f, maxZ}); // 3
        // (0,3,2) and (0,2,1) both yield a +Y normal.
        indices.PushBack(base + 0);
        indices.PushBack(base + 3);
        indices.PushBack(base + 2);
        indices.PushBack(base + 0);
        indices.PushBack(base + 2);
        indices.PushBack(base + 1);
    }

    // A solid box obstacle (top + 4 walls, from y=0 to y=height) over [minX,maxX] x [minZ,maxZ].
    // Winding is irrelevant: the box only needs to rasterize as a tall solid so the ground under
    // its footprint becomes non-walkable (a hole the agent routes around).
    void AddBox(Array<Float3>& verts, Array<u32>& indices, f32 minX, f32 maxX, f32 minZ, f32 maxZ,
                f32 height)
    {
        auto quad = [&](Float3 a, Float3 b, Float3 c, Float3 d)
        {
            const u32 base = static_cast<u32>(verts.Size());
            verts.PushBack(a);
            verts.PushBack(b);
            verts.PushBack(c);
            verts.PushBack(d);
            indices.PushBack(base + 0);
            indices.PushBack(base + 1);
            indices.PushBack(base + 2);
            indices.PushBack(base + 0);
            indices.PushBack(base + 2);
            indices.PushBack(base + 3);
        };
        const f32 h = height;
        // top
        quad(Float3{minX, h, minZ}, Float3{maxX, h, minZ}, Float3{maxX, h, maxZ},
             Float3{minX, h, maxZ});
        // walls
        quad(Float3{minX, 0, minZ}, Float3{maxX, 0, minZ}, Float3{maxX, h, minZ},
             Float3{minX, h, minZ});
        quad(Float3{maxX, 0, maxZ}, Float3{minX, 0, maxZ}, Float3{minX, h, maxZ},
             Float3{maxX, h, maxZ});
        quad(Float3{minX, 0, maxZ}, Float3{minX, 0, minZ}, Float3{minX, h, minZ},
             Float3{minX, h, maxZ});
        quad(Float3{maxX, 0, minZ}, Float3{maxX, 0, maxZ}, Float3{maxX, h, maxZ},
             Float3{maxX, h, minZ});
    }

    bool BytesEqual(const Array<byte>& a, const Array<byte>& b)
    {
        if (a.Size() != b.Size())
        {
            return false;
        }
        return a.Size() == 0 || std::memcmp(a.Data(), b.Data(), a.Size()) == 0;
    }

    bool Finite(Float3 v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    f32 DistXZ(Float3 a, Float3 b)
    {
        const f32 dx = a.x - b.x, dz = a.z - b.z;
        return std::sqrt(dx * dx + dz * dz);
    }
}

TEST_CASE("navigation: bake is deterministic and profile-sensitive")
{
    Array<Float3> verts;
    Array<u32> indices;
    AddGround(verts, indices, -10, 10, -10, 10);
    AddBox(verts, indices, -2, 2, -2, 2, 3.0f);

    const Span<const Float3> vspan{verts.Data(), verts.Size()};
    const Span<const u32> ispan{indices.Data(), indices.Size()};

    NavigationBakeParams params;
    Array<byte> a, b;
    REQUIRE(NavigationMeshBuilder::Build(vspan, ispan, params, a).IsOk());
    REQUIRE(NavigationMeshBuilder::Build(vspan, ispan, params, b).IsOk());
    CHECK(a.Size() > 0);
    CHECK(BytesEqual(a, b)); // byte-identical across runs

    NavigationBakeParams coarser = params;
    coarser.cellSize = 0.5f; // a different voxel resolution must change the output
    Array<byte> c;
    REQUIRE(NavigationMeshBuilder::Build(vspan, ispan, coarser, c).IsOk());
    CHECK_FALSE(BytesEqual(a, c));

    // Degenerate input is rejected, not crashed.
    Array<byte> empty;
    CHECK_FALSE(NavigationMeshBuilder::Build(Span<const Float3>{}, Span<const u32>{}, params, empty)
                    .IsOk());
    Array<u32> badIndices;
    badIndices.PushBack(0);
    badIndices.PushBack(1); // not a multiple of 3
    CHECK_FALSE(
        NavigationMeshBuilder::Build(vspan, Span<const u32>{badIndices.Data(), badIndices.Size()},
                                     params, empty)
            .IsOk());
}

TEST_CASE("navigation: query paths around an obstacle and reports the unreachable")
{
    Array<Float3> verts;
    Array<u32> indices;
    AddGround(verts, indices, -10, 10, -10, 10);
    AddBox(verts, indices, -2, 2, -2, 2, 3.0f);

    Array<byte> blob;
    REQUIRE(NavigationMeshBuilder::Build(Span<const Float3>{verts.Data(), verts.Size()},
                                         Span<const u32>{indices.Data(), indices.Size()},
                                         NavigationBakeParams{}, blob)
                .IsOk());

    NavigationMesh mesh;
    REQUIRE(mesh.Load(Span<const byte>{blob.Data(), blob.Size()}).IsOk());
    REQUIRE(mesh.IsValid());

    NavigationMeshQuery query(mesh);
    REQUIRE(query.IsValid());

    // Straight line from (-8,0,0) to (8,0,0) passes through the box; the path must detour.
    NavigationPath path;
    REQUIRE(query.FindPath(Float3{-8, 0, 0}, Float3{8, 0, 0}, path).IsOk());
    CHECK(path.complete);
    REQUIRE(path.corners.Size() >= 3u); // start + at least one detour corner + end
    f32 maxAbsZ = 0.0f;
    for (const Float3& c : path.corners)
    {
        maxAbsZ = std::max(maxAbsZ, std::abs(c.z));
    }
    CHECK(maxAbsZ > 1.5f); // routed clear of the box (half-extent 2, straight line is z=0)

    // Off-mesh destination: a hard failure, never a crash or a false success.
    NavigationPath offMesh;
    CHECK_FALSE(query.FindPath(Float3{-8, 0, 0}, Float3{100, 0, 100}, offMesh).IsOk());
    CHECK_FALSE(offMesh.complete);
}

TEST_CASE("navigation: disconnected destination is reported incomplete, not failed")
{
    // Two separate ground islands with a gap between them: both ends snap onto the mesh, but no
    // path connects them.
    Array<Float3> verts;
    Array<u32> indices;
    AddGround(verts, indices, -10, -3, -5, 5); // island A
    AddGround(verts, indices, 3, 10, -5, 5);   // island B

    Array<byte> blob;
    REQUIRE(NavigationMeshBuilder::Build(Span<const Float3>{verts.Data(), verts.Size()},
                                         Span<const u32>{indices.Data(), indices.Size()},
                                         NavigationBakeParams{}, blob)
                .IsOk());
    NavigationMesh mesh;
    REQUIRE(mesh.Load(Span<const byte>{blob.Data(), blob.Size()}).IsOk());
    NavigationMeshQuery query(mesh);
    REQUIRE(query.IsValid());

    NavigationPath path;
    const Status st = query.FindPath(Float3{-6, 0, 0}, Float3{6, 0, 0}, path);
    CHECK(st.IsOk());          // both ends are ON the mesh
    CHECK_FALSE(path.complete); // ...but the destination is unreachable
}

TEST_CASE("navigation: DebugTriangles enumerates the live navmesh surface")
{
    Array<Float3> verts;
    Array<u32> indices;
    AddGround(verts, indices, -10, 10, -10, 10);

    Array<byte> blob;
    REQUIRE(NavigationMeshBuilder::Build(Span<const Float3>{verts.Data(), verts.Size()},
                                         Span<const u32>{indices.Data(), indices.Size()},
                                         NavigationBakeParams{}, blob)
                .IsOk());
    NavigationMesh mesh;
    REQUIRE(mesh.Load(Span<const byte>{blob.Data(), blob.Size()}).IsOk());

    Array<Float3> tris;
    mesh.DebugTriangles(tris);
    REQUIRE(tris.Size() > 0u);
    CHECK(tris.Size() % 3u == 0u); // whole triangles

    // Every vertex sits inside the ground extents inflated by one cell (0.3).
    const f32 pad = 0.3f + 1e-3f;
    for (const Float3& v : tris)
    {
        CHECK(v.x >= -10.0f - pad);
        CHECK(v.x <= 10.0f + pad);
        CHECK(v.z >= -10.0f - pad);
        CHECK(v.z <= 10.0f + pad);
    }

    // Append semantics: a second call adds, does not replace.
    const usize firstCount = tris.Size();
    mesh.DebugTriangles(tris);
    CHECK(tris.Size() == firstCount * 2u);
}

TEST_CASE("navigation: two agents cross head-on, both arrive, no hard overlap")
{
    Array<Float3> verts;
    Array<u32> indices;
    AddGround(verts, indices, -10, 10, -10, 10); // open field, no obstacle

    Array<byte> blob;
    REQUIRE(NavigationMeshBuilder::Build(Span<const Float3>{verts.Data(), verts.Size()},
                                         Span<const u32>{indices.Data(), indices.Size()},
                                         NavigationBakeParams{}, blob)
                .IsOk());
    NavigationMesh mesh;
    REQUIRE(mesh.Load(Span<const byte>{blob.Data(), blob.Size()}).IsOk());

    NavigationCrowd crowd(mesh, 4, 0.6f);
    REQUIRE(crowd.IsValid());

    NavigationAgentParams ap; // defaults: radius 0.6, maxSpeed 3.5
    const i32 a = crowd.AddAgent(Float3{-5, 0, 0}, ap);
    const i32 b = crowd.AddAgent(Float3{5, 0, 0}, ap);
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);
    CHECK(crowd.IsAgentValid(a));
    CHECK(crowd.IsAgentValid(b));

    const Float3 targetA{5, 0, 0};
    const Float3 targetB{-5, 0, 0};
    CHECK(crowd.SetTarget(a, targetA));
    CHECK(crowd.SetTarget(b, targetB));

    // Deterministic stepped simulation: 12s at 30 Hz. Track the closest approach.
    f32 minSeparation = 1e9f;
    const f32 dt = 1.0f / 30.0f;
    for (int step = 0; step < 360; ++step)
    {
        crowd.Update(dt);
        const Float3 pa = crowd.AgentPosition(a);
        const Float3 pb = crowd.AgentPosition(b);
        REQUIRE(Finite(pa));
        REQUIRE(Finite(pb));
        minSeparation = std::min(minSeparation, DistXZ(pa, pb));
    }

    const Float3 finalA = crowd.AgentPosition(a);
    const Float3 finalB = crowd.AgentPosition(b);
    // Both advanced well past their starts and reached the far side.
    CHECK(DistXZ(finalA, targetA) < 1.5f);
    CHECK(DistXZ(finalB, targetB) < 1.5f);
    // They avoided each other rather than passing through (radii sum 1.2; allow steering slack).
    CHECK(minSeparation > 0.5f);
}

TEST_CASE("tiled bake: a large ground splits into tiles, paths span them, and it is deterministic")
{
    // 60x60 ground at cellSize 0.3 / tileCells 64 = 19.2u tiles -> a 4x4 grid.
    Array<Float3> verts;
    Array<u32> indices;
    AddGround(verts, indices, -30.0f, 30.0f, -30.0f, 30.0f);
    const Span<const Float3> vspan{verts.Data(), verts.Size()};
    const Span<const u32> ispan{indices.Data(), indices.Size()};
    NavigationBakeParams params;

    Array<byte> blobA;
    Array<byte> blobB;
    REQUIRE(NavigationMeshBuilder::BuildTiled(vspan, ispan, params, blobA).IsOk());
    REQUIRE(NavigationMeshBuilder::BuildTiled(vspan, ispan, params, blobB).IsOk());
    REQUIRE(blobA.Size() == blobB.Size()); // determinism holds for the tiled path too
    CHECK(std::memcmp(blobA.Data(), blobB.Data(), blobA.Size()) == 0);

    NavigationMesh mesh;
    REQUIRE(mesh.Load(Span<const byte>{blobA.Data(), blobA.Size()}).IsOk());
    REQUIRE(mesh.IsValid());
    CHECK(mesh.BakedAgentRadius() == doctest::Approx(params.agentRadius));

    // A corner-to-corner path crosses many tile boundaries; the stitched mesh must carry it
    // end to end.
    NavigationMeshQuery query(mesh);
    REQUIRE(query.IsValid());
    NavigationPath path;
    REQUIRE(query.FindPath(Float3{-27, 0, -27}, Float3{27, 0, 27}, path).IsOk());
    CHECK(path.complete);
    REQUIRE(path.corners.Size() >= 2u);
    const Float3 last = path.corners[path.corners.Size() - 1];
    CHECK(std::abs(last.x - 27.0f) < 1.0f);
    CHECK(std::abs(last.z - 27.0f) < 1.0f);

    // A crowd agent walks the span too (the runtime consumers see one seamless mesh).
    NavigationCrowd crowd(mesh, 4, 0.6f);
    REQUIRE(crowd.IsValid());
    NavigationAgentParams agent;
    const i32 id = crowd.AddAgent(Float3{-27, 0, -27}, agent);
    REQUIRE(id >= 0);
    REQUIRE(crowd.SetTarget(id, Float3{27, 0, 27}));
    for (int step = 0; step < 150; ++step) // 5s at 3.5u/s ~ 12u along the diagonal
    {
        crowd.Update(1.0f / 30.0f);
    }
    const Float3 pos = crowd.AgentPosition(id);
    CHECK(pos.x > -20.0f); // moving toward the far corner across tile seams
    CHECK(pos.z > -20.0f);
}

TEST_CASE("tiled bake: BuildTileAt regenerates a tile byte-identical to the full bake's")
{
    Array<Float3> verts;
    Array<u32> indices;
    AddGround(verts, indices, -30.0f, 30.0f, -30.0f, 30.0f);
    const Span<const Float3> vspan{verts.Data(), verts.Size()};
    const Span<const u32> ispan{indices.Data(), indices.Size()};
    NavigationBakeParams params;

    Array<byte> blob;
    REQUIRE(NavigationMeshBuilder::BuildTiled(vspan, ispan, params, blob).IsOk());

    // Walk the blob to the record for tile (1, 2).
    struct BlobHeaderMirror
    {
        u32 magic, version;
        f32 agentRadius, agentHeight;
        u32 navDataSize;
    };
    struct TiledInfoMirror
    {
        f32 origin[3], tileWorldSize;
        i32 countX, countY;
        u32 tileCount;
    };
    struct TileRecordMirror
    {
        i32 tx, ty;
        u32 dataSize;
    };
    const byte* cursor = blob.Data() + sizeof(BlobHeaderMirror);
    TiledInfoMirror info;
    std::memcpy(&info, cursor, sizeof(info));
    cursor += sizeof(info);
    REQUIRE(info.countX == 4);
    REQUIRE(info.countY == 4);
    REQUIRE(info.tileCount >= 16u - 4u); // most tiles hold ground

    bool found = false;
    for (u32 i = 0; i < info.tileCount; ++i)
    {
        TileRecordMirror record;
        std::memcpy(&record, cursor, sizeof(record));
        cursor += sizeof(record);
        if (record.tx == 1 && record.ty == 2)
        {
            Array<byte> regenerated;
            REQUIRE(NavigationMeshBuilder::BuildTileAt(vspan, ispan, params, 1, 2, regenerated)
                        .IsOk());
            REQUIRE(regenerated.Size() == static_cast<usize>(record.dataSize));
            CHECK(std::memcmp(regenerated.Data(), cursor, record.dataSize) == 0);
            found = true;
            break;
        }
        cursor += record.dataSize;
    }
    CHECK(found);

    // Out-of-grid coordinates are rejected, not crashed on.
    Array<byte> bogus;
    CHECK_FALSE(NavigationMeshBuilder::BuildTileAt(vspan, ispan, params, 99, 0, bogus).IsOk());
}

TEST_CASE("tiled bake: v1 single-tile blobs still load (the reader sniffs the version)")
{
    Array<Float3> verts;
    Array<u32> indices;
    AddGround(verts, indices, -5.0f, 5.0f, -5.0f, 5.0f);
    NavigationBakeParams params;
    Array<byte> v1;
    REQUIRE(NavigationMeshBuilder::Build(Span<const Float3>{verts.Data(), verts.Size()},
                                         Span<const u32>{indices.Data(), indices.Size()},
                                         params, v1)
                .IsOk());
    NavigationMesh mesh;
    REQUIRE(mesh.Load(Span<const byte>{v1.Data(), v1.Size()}).IsOk());
    CHECK(mesh.IsValid());
}

TEST_CASE("tiled bake: stage capture yields contours + walkable span samples in-bounds")
{
    Array<Float3> verts;
    Array<u32> indices;
    AddGround(verts, indices, -10.0f, 10.0f, -10.0f, 10.0f);
    NavigationBakeParams params;
    Array<byte> blob;
    NavigationBakeStages stages;
    REQUIRE(NavigationMeshBuilder::BuildTiled(Span<const Float3>{verts.Data(), verts.Size()},
                                              Span<const u32>{indices.Data(), indices.Size()},
                                              params, blob, &stages)
                .IsOk());

    REQUIRE(!stages.contourLines.IsEmpty());
    CHECK(stages.contourLines.Size() % 2u == 0u); // segment PAIRS
    REQUIRE(!stages.walkableSamples.IsEmpty());

    // Everything captured sits on/near the baked geometry (border expansion allows a small
    // apron beyond the ground bounds).
    const f32 apron = 3.0f;
    for (const Float3& p : stages.contourLines)
    {
        CHECK(p.x > -10.0f - apron);
        CHECK(p.x < 10.0f + apron);
        CHECK(p.z > -10.0f - apron);
        CHECK(p.z < 10.0f + apron);
    }
    for (const Float3& p : stages.walkableSamples)
    {
        CHECK(std::abs(p.y) < 1.0f); // span tops hug the y=0 ground
    }

    // Capture is an observer: the blob is byte-identical with and without it.
    Array<byte> plain;
    REQUIRE(NavigationMeshBuilder::BuildTiled(Span<const Float3>{verts.Data(), verts.Size()},
                                              Span<const u32>{indices.Data(), indices.Size()},
                                              params, plain)
                .IsOk());
    REQUIRE(plain.Size() == blob.Size());
    CHECK(std::memcmp(plain.Data(), blob.Data(), plain.Size()) == 0);
}
