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

TEST_CASE("terrain: LOD selection is deterministic by distance")
{
    const f32 ranges[] = {50.0f, 150.0f, 400.0f}; // far edge of LOD 0,1,2
    const Span<const f32> lod{ranges, 3};
    CHECK(SelectLod(0.0f, lod) == 0u);
    CHECK(SelectLod(50.0f, lod) == 0u);   // inclusive far edge
    CHECK(SelectLod(50.1f, lod) == 1u);
    CHECK(SelectLod(150.0f, lod) == 1u);
    CHECK(SelectLod(151.0f, lod) == 2u);
    CHECK(SelectLod(1000.0f, lod) == 3u); // beyond the last -> coarsest

    // Distance to a chunk uses the closest point on its bounds (0 inside).
    RefPtr<hf::Heightfield> h = MakeRampX();
    Array<TerrainChunk> chunks;
    BuildChunks(*h, chunks);
    // A camera far above the centre is closer to the near chunks than the far ones... here all
    // chunks are equidistant in XZ, so just check monotonicity vs a fixed camera off to the west.
    const Float3 cam{-200.0f, 0.0f, 0.0f};
    const f32 dWest = DistanceToChunk(chunks[0], cam);  // west chunk
    const f32 dEast = DistanceToChunk(chunks[1], cam);  // east chunk
    CHECK(dWest < dEast);
    CHECK(dWest == doctest::Approx(136.0f).epsilon(0.05)); // 200 - 64 to the west edge
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

TEST_CASE("terrain: splat layer descriptor defaults")
{
    SplatLayer layer;
    CHECK(layer.tileScale == doctest::Approx(1.0f));
}
