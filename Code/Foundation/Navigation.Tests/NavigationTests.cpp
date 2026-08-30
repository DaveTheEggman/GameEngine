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
