// foundation.terrain: the terrain model over a heightfield - chunk grid + bounds, per-chunk LOD
// selection determinism, quadtree correctness (build + frustum cull), and the splat descriptor.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.heightfield;
import foundation.terrain;

using namespace foundation::core;
using namespace foundation::terrain;
namespace hf = foundation::heightfield;

namespace
{
    // A 129-grid (2x2 chunks) over 128x128 world, Y range [0,10], rising along +X.
    RefPtr<hf::Heightfield> MakeRampX()
    {
        RefPtr<hf::Heightfield> h =
            MakeRef<hf::Heightfield>(DefaultAllocator(), 129, Float2{128.0f, 128.0f}, 0.0f, 10.0f);
        for (i32 z = 0; z < 129; ++z)
        {
            for (i32 x = 0; x < 129; ++x)
            {
                h->SetSample(x, z, static_cast<hf::Height>(static_cast<f32>(x) / 128.0f * 65535.0f));
            }
        }
        return h;
    }

    bool Near(f32 a, f32 b, f32 eps = 1.0e-2f) { return Abs(a - b) <= eps; }
}

TEST_CASE("terrain: chunks-per-side follows the size contract")
{
    CHECK(ChunksPerSide(65) == 1);
    CHECK(ChunksPerSide(129) == 2);
    CHECK(ChunksPerSide(257) == 4);
    CHECK(ChunksPerSide(193) == 3); // 64k+1 need not be a power of two
    CHECK(ChunksPerSide(1025) == 16);
}

TEST_CASE("terrain: build chunks - grid, footprint, and per-chunk Y bounds")
{
    RefPtr<hf::Heightfield> h = MakeRampX();
    Array<TerrainChunk> chunks;
    BuildChunks(*h, chunks);
    REQUIRE(chunks.Size() == 4u); // 2x2

    // Row-major: (cx, cz) at index cz*2 + cx.
    const TerrainChunk& c00 = chunks[0]; // west-south
    const TerrainChunk& c10 = chunks[1]; // east-south
    CHECK(c00.chunkX == 0);
    CHECK(c00.chunkZ == 0);
    CHECK(c10.chunkX == 1);

    // Footprint quadrants: chunk 0 spans world X [-64, 0], chunk 1 spans [0, 64].
    CHECK(Near(c00.bounds.min.x, -64.0f));
    CHECK(Near(c00.bounds.max.x, 0.0f));
    CHECK(Near(c10.bounds.min.x, 0.0f));
    CHECK(Near(c10.bounds.max.x, 64.0f));

    // The ramp rises along +X: the west chunk's Y range sits below the east chunk's.
    CHECK(c00.bounds.min.y == doctest::Approx(0.0f));
    CHECK(c00.bounds.max.y < c10.bounds.max.y);
    CHECK(c10.bounds.max.y == doctest::Approx(10.0f).epsilon(0.01));
}

TEST_CASE("terrain: per-chunk LOD via the shared coverage metric (foundation.lod)")
{
    RefPtr<hf::Heightfield> h = MakeRampX();
    Array<TerrainChunk> chunks;
    BuildChunks(*h, chunks);

    // Descending coverage thresholds (thresholds[0] = 1.0 by convention, level 0 = fallback).
    const f32 thresholds[] = {1.0f, 0.2f, 0.05f};
    const Span<const f32> lod{thresholds, 3};

    const Float4x4 identity = Float4x4::Identity(); // terrain at the origin
    const Float4x4 proj = Float4x4::PerspectiveFovRH(1.0f, 1.0f, 1.0f, 5000.0f);
    const Float4x4 nearView =
        Float4x4::LookAtRH(Float3{0.0f, 50.0f, 0.1f}, Float3{0.0f, 0.0f, 0.0f}, Float3{0.0f, 0.0f, 1.0f});
    const Float4x4 farView = Float4x4::LookAtRH(Float3{0.0f, 3000.0f, 0.1f}, Float3{0.0f, 0.0f, 0.0f},
                                                Float3{0.0f, 0.0f, 1.0f});

    const u32 lodNear = SelectChunkLod(chunks[0], identity, nearView, proj, lod);
    const u32 lodFar = SelectChunkLod(chunks[0], identity, farView, proj, lod);
    CHECK(lodNear <= lodFar); // closer is never coarser
    CHECK(lodNear == 0u);     // close -> high coverage -> finest
    CHECK(lodFar > 0u);       // very far -> low coverage -> coarser

    // Batch matches the per-chunk call.
    Array<u32> lods;
    SelectChunkLods(Span<const TerrainChunk>(chunks.Data(), chunks.Size()), identity, nearView, proj,
                    lod, 0.0f, lods);
    REQUIRE(lods.Size() == chunks.Size());
    CHECK(lods[0] == lodNear);

    // ChunkBoundingSphere (the bridge to the coverage metric) encloses the box.
    const BoundingSphere s = ChunkBoundingSphere(chunks[0]);
    CHECK(Near(s.center.x, chunks[0].bounds.Center().x));
    CHECK(s.radius >= Length(chunks[0].bounds.Extents()) - 1.0e-3f);
}

TEST_CASE("terrain: quadtree builds over the chunk grid")
{
    RefPtr<hf::Heightfield> h = MakeRampX();
    Array<TerrainChunk> chunks;
    BuildChunks(*h, chunks);

    TerrainQuadtree tree;
    tree.Build(Span<const TerrainChunk>(chunks.Data(), chunks.Size()), ChunksPerSide(h->Size()));
    CHECK_FALSE(tree.IsEmpty());
    // 2x2: root + 4 leaves = 5 nodes.
    CHECK(tree.NodeCount() == 5u);
}

TEST_CASE("terrain: quadtree cull - all visible, none visible")
{
    RefPtr<hf::Heightfield> h = MakeRampX();
    Array<TerrainChunk> chunks;
    BuildChunks(*h, chunks);
    TerrainQuadtree tree;
    tree.Build(Span<const TerrainChunk>(chunks.Data(), chunks.Size()), ChunksPerSide(h->Size()));

    const Float4x4 proj = Float4x4::PerspectiveFovRH(1.2f, 1.0f, 1.0f, 2000.0f);

    // Looking down at the terrain from above: every chunk is inside the frustum.
    {
        const Float4x4 view =
            Float4x4::LookAtRH(Float3{0.0f, 300.0f, 0.1f}, Float3{0.0f, 0.0f, 0.0f},
                               Float3{0.0f, 0.0f, 1.0f});
        const BoundingFrustum frustum(view * proj);
        Array<i32> visible;
        tree.Cull(frustum, visible);
        CHECK(visible.Size() == 4u);
    }

    // Looking away from the terrain (far off to +X, gazing further +X): nothing is visible.
    {
        const Float4x4 view =
            Float4x4::LookAtRH(Float3{5000.0f, 100.0f, 0.0f}, Float3{6000.0f, 100.0f, 0.0f},
                               Float3{0.0f, 1.0f, 0.0f});
        const BoundingFrustum frustum(view * proj);
        Array<i32> visible;
        tree.Cull(frustum, visible);
        CHECK(visible.Size() == 0u);
    }
}

TEST_CASE("terrain: shared chunk grid mesh - vertices + per-LOD indices")
{
    Array<Float2> verts;
    BuildChunkGridVertices(verts);
    REQUIRE(verts.Size() == static_cast<usize>(kChunkVerts) * kChunkVerts); // 65*65
    CHECK(Near(verts[0].x, 0.0f));
    CHECK(Near(verts[0].y, 0.0f));
    CHECK(Near(verts[verts.Size() - 1].x, 1.0f)); // last vertex = (1,1)
    CHECK(Near(verts[verts.Size() - 1].y, 1.0f));

    Array<u32> lod0;
    BuildChunkGridIndices(0, lod0);
    CHECK(lod0.Size() == 64u * 64u * 6u); // full density: 64x64 quads, 2 tris each

    Array<u32> lod1;
    BuildChunkGridIndices(1, lod1);
    CHECK(lod1.Size() == 32u * 32u * 6u); // stride 2

    Array<u32> lod6;
    BuildChunkGridIndices(kMaxChunkLod, lod6);
    CHECK(lod6.Size() == 6u); // one quad

    Array<u32> lodTooCoarse;
    BuildChunkGridIndices(kMaxChunkLod + 1, lodTooCoarse);
    CHECK(lodTooCoarse.IsEmpty());

    // Every index references a real grid vertex.
    bool allInRange = true;
    for (usize i = 0; i < lod0.Size(); ++i)
    {
        if (lod0[i] >= verts.Size())
        {
            allInRange = false;
            break;
        }
    }
    CHECK(allInRange);
}

TEST_CASE("terrain: extract visible chunk draws (cull + LOD -> draw list)")
{
    RefPtr<hf::Heightfield> h = MakeRampX();
    Array<TerrainChunk> chunks;
    BuildChunks(*h, chunks);
    TerrainQuadtree tree;
    tree.Build(Span<const TerrainChunk>(chunks.Data(), chunks.Size()), ChunksPerSide(h->Size()));

    const f32 thresholds[] = {1.0f, 0.2f, 0.05f};
    const Span<const f32> lod{thresholds, 3};
    const Float4x4 identity = Float4x4::Identity();
    const Float4x4 proj = Float4x4::PerspectiveFovRH(1.2f, 1.0f, 1.0f, 5000.0f);

    // Looking down from above: all 4 chunks visible, each with a selected LOD.
    {
        const Float4x4 view = Float4x4::LookAtRH(Float3{0.0f, 300.0f, 0.1f},
                                                 Float3{0.0f, 0.0f, 0.0f}, Float3{0.0f, 0.0f, 1.0f});
        const BoundingFrustum frustum(view * proj);
        Array<ChunkDraw> draws;
        ExtractVisibleChunkDraws(tree, Span<const TerrainChunk>(chunks.Data(), chunks.Size()),
                                 identity, view, proj, frustum, lod, 0.0f, draws);
        CHECK(draws.Size() == 4u);
        for (usize i = 0; i < draws.Size(); ++i)
        {
            CHECK(draws[i].chunkIndex >= 0);
            CHECK(draws[i].chunkIndex < 4);
            CHECK(draws[i].lod <= 3u);
        }
    }

    // Looking away: nothing to draw.
    {
        const Float4x4 view = Float4x4::LookAtRH(Float3{5000.0f, 100.0f, 0.0f},
                                                 Float3{6000.0f, 100.0f, 0.0f}, Float3{0, 1, 0});
        const BoundingFrustum frustum(view * proj);
        Array<ChunkDraw> draws;
        ExtractVisibleChunkDraws(tree, Span<const TerrainChunk>(chunks.Data(), chunks.Size()),
                                 identity, view, proj, frustum, lod, 0.0f, draws);
        CHECK(draws.Size() == 0u);
    }
}

TEST_CASE("terrain: splat layer descriptor defaults")
{
    SplatLayer layer;
    CHECK(layer.tileScale == doctest::Approx(1.0f));
}
