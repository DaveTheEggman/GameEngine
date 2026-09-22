// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// foundation.vegetation - the scatter's contract: determinism per seed, density -> count, the
// splat / slope / height rules, normal alignment, the fade math, chunk touch mapping, the cap.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

#include <initializer_list>

import foundation.core;
import foundation.heightfield;
import foundation.terrain;
import foundation.terrain.resource;
import foundation.vegetation;

using namespace foundation::core;
namespace hf = foundation::heightfield;
namespace tmodel = foundation::terrain;
namespace veg = foundation::vegetation;

namespace
{
    constexpr i32 kGrid = 65;     // one 64-quad chunk
    constexpr f32 kWorld = 64.0f; // 1 m per quad: the chunk is 64 x 64 m = 4096 m^2

    // A flat field at `height` (world Y range 0..40).
    RefPtr<hf::Heightfield> MakeFlat(f32 height)
    {
        auto grid = MakeRef<hf::Heightfield>(DefaultAllocator(), kGrid, Float2{kWorld, kWorld},
                                             0.0f, 40.0f);
        const hf::Height sample = grid->WorldYToSample(height);
        for (i32 z = 0; z < kGrid; ++z)
        {
            for (i32 x = 0; x < kGrid; ++x)
            {
                grid->SetSample(x, z, sample);
            }
        }
        return grid;
    }

    // A plane rising with x: 0 at the -x edge, `rise` at the +x edge (a constant slope).
    RefPtr<hf::Heightfield> MakeRamp(f32 rise)
    {
        auto grid = MakeRef<hf::Heightfield>(DefaultAllocator(), kGrid, Float2{kWorld, kWorld},
                                             0.0f, 40.0f);
        for (i32 z = 0; z < kGrid; ++z)
        {
            for (i32 x = 0; x < kGrid; ++x)
            {
                const f32 t = static_cast<f32>(x) / static_cast<f32>(kGrid - 1);
                grid->SetSample(x, z, grid->WorldYToSample(t * rise));
            }
        }
        return grid;
    }

    // The single chunk of a 65-sample grid.
    tmodel::TerrainChunk ChunkOf(const hf::Heightfield& grid)
    {
        Array<tmodel::TerrainChunk> chunks;
        tmodel::BuildChunks(grid, chunks);
        REQUIRE(chunks.Size() == 1u);
        return chunks[0];
    }

    // A splat raster: palette layer 0 one-hot on the left half (x < 0), base on the right.
    RefPtr<tmodel::SplatWeights> MakeHalfSplat()
    {
        constexpr i32 n = 32;
        auto sw = MakeRef<tmodel::SplatWeights>(DefaultAllocator(), n, n);
        Span<u8> idx = sw->Indices();
        Span<u8> wts = sw->Weights();
        for (i32 y = 0; y < n; ++y)
        {
            for (i32 x = 0; x < n / 2; ++x)
            {
                const usize at = sw->TexelOffset(x, y);
                idx[at + 0] = 0;
                wts[at + 0] = 255;
            }
        }
        sw->BumpVersion();
        return sw;
    }

    veg::VegetationLayer Uniform(f32 density)
    {
        veg::VegetationLayer layer;
        layer.placement = veg::VegetationPlacement::Uniform;
        layer.density = density;
        layer.maxSlopeDegrees = 90.0f;
        return layer;
    }

    bool SameTransforms(const Array<Float4x4>& a, const Array<Float4x4>& b)
    {
        if (a.Size() != b.Size())
        {
            return false;
        }
        for (usize i = 0; i < a.Size(); ++i)
        {
            if (MemCompare(&a[i], &b[i], sizeof(Float4x4)) != 0)
            {
                return false;
            }
        }
        return true;
    }

    Float3 Row(const Float4x4& m, u32 r)
    {
        return Float3{m.m[r][0], m.m[r][1], m.m[r][2]};
    }
}

TEST_CASE("vegetation scatter: a seed gives byte-identical instances; another chunk or layer differs")
{
    RefPtr<hf::Heightfield> grid = MakeFlat(5.0f);
    const tmodel::TerrainChunk chunk = ChunkOf(*grid);
    const veg::VegetationLayer layer = Uniform(0.25f);
    const Guid layerId{0x1234u, 0x5678u};
    const u64 seed = veg::ChunkSeed(layerId, 0, 0, 0);

    veg::ScatterResult a;
    veg::ScatterResult b;
    veg::ScatterChunk(seed, chunk, *grid, nullptr, layer, AABB::Empty(), a);
    veg::ScatterChunk(seed, chunk, *grid, nullptr, layer, AABB::Empty(), b);
    REQUIRE(!a.transforms.IsEmpty());
    CHECK(SameTransforms(a.transforms, b.transforms));
    // Every instance sits ON the surface, inside the chunk footprint.
    for (const Float4x4& m : a.transforms)
    {
        CHECK(m.m[3][1] == doctest::Approx(5.0f).epsilon(0.01));
        CHECK(m.m[3][0] >= chunk.bounds.min.x);
        CHECK(m.m[3][0] <= chunk.bounds.max.x);
        CHECK(m.m[3][2] >= chunk.bounds.min.z);
        CHECK(m.m[3][2] <= chunk.bounds.max.z);
    }

    // The seed is the identity: a neighbouring chunk, another layer index or another owner
    // scatters differently.
    CHECK(veg::ChunkSeed(layerId, 0, 1, 0) != seed);
    CHECK(veg::ChunkSeed(layerId, 0, 0, 1) != seed);
    CHECK(veg::ChunkSeed(layerId, 1, 0, 0) != seed);
    CHECK(veg::ChunkSeed(Guid{0x1234u, 0x5679u}, 0, 0, 0) != seed);
    veg::ScatterResult c;
    veg::ScatterChunk(veg::ChunkSeed(layerId, 0, 1, 0), chunk, *grid, nullptr, layer,
                      AABB::Empty(), c);
    CHECK(!SameTransforms(a.transforms, c.transforms));
}

TEST_CASE("vegetation scatter: density scales the candidate count; the per-chunk cap scales density down")
{
    RefPtr<hf::Heightfield> grid = MakeFlat(1.0f);
    const tmodel::TerrainChunk chunk = ChunkOf(*grid);

    veg::ScatterResult one;
    veg::ScatterChunk(7, chunk, *grid, nullptr, Uniform(0.25f), AABB::Empty(), one);
    veg::ScatterResult two;
    veg::ScatterChunk(7, chunk, *grid, nullptr, Uniform(0.5f), AABB::Empty(), two);
    CHECK(one.candidateCount == 1024u); // 0.25 / m^2 x 4096 m^2
    CHECK(two.candidateCount == 2048u);
    CHECK(one.transforms.Size() == 1024u); // Uniform on a flat field keeps every candidate
    CHECK(two.transforms.Size() == 2048u);
    CHECK(!one.densityClamped);
    CHECK(one.effectiveDensity == doctest::Approx(0.25f));

    // Over budget: the cap wins and the density reports what was actually used.
    veg::VegetationLayer dense = Uniform(10.0f); // 40960 wanted
    dense.maxInstancesPerChunk = 4096;
    veg::ScatterResult capped;
    veg::ScatterChunk(7, chunk, *grid, nullptr, dense, AABB::Empty(), capped);
    CHECK(capped.candidateCount == 4096u);
    CHECK(capped.densityClamped);
    CHECK(capped.effectiveDensity == doctest::Approx(1.0f));
    CHECK(capped.transforms.Size() == 4096u);

    // Nothing to do: zero density, an empty heightfield, or an authored (Scattered) layer.
    veg::ScatterResult none;
    veg::ScatterChunk(7, chunk, *grid, nullptr, Uniform(0.0f), AABB::Empty(), none);
    CHECK(none.transforms.IsEmpty());
    veg::VegetationLayer authored = Uniform(1.0f);
    authored.placement = veg::VegetationPlacement::Scattered;
    veg::ScatterChunk(7, chunk, *grid, nullptr, authored, AABB::Empty(), none);
    CHECK(none.transforms.IsEmpty());
}

TEST_CASE("vegetation scatter: the splat rule grows only where the layer's share clears the threshold")
{
    RefPtr<hf::Heightfield> grid = MakeFlat(2.0f);
    const tmodel::TerrainChunk chunk = ChunkOf(*grid);
    RefPtr<tmodel::SplatWeights> splat = MakeHalfSplat();

    veg::VegetationLayer grass;
    grass.placement = veg::VegetationPlacement::Splat;
    grass.splatLayer = 0;
    grass.splatThreshold = 0.25f;
    grass.density = 0.5f;
    grass.maxSlopeDegrees = 90.0f;

    veg::ScatterResult painted;
    veg::ScatterChunk(3, chunk, *grid, splat.Get(), grass, AABB::Empty(), painted);
    REQUIRE(!painted.transforms.IsEmpty());
    // Roughly half the candidates (the painted half at share 1 keeps every one of them).
    CHECK(painted.transforms.Size() > painted.candidateCount / 3);
    CHECK(painted.transforms.Size() < painted.candidateCount * 2 / 3);
    for (const Float4x4& m : painted.transforms)
    {
        CHECK(m.m[3][0] < 0.0f); // only on the painted (x < 0) half
    }

    // The base layer is the complement.
    veg::VegetationLayer base = grass;
    base.splatLayer = veg::kSplatBaseLayer;
    veg::ScatterResult unpainted;
    veg::ScatterChunk(3, chunk, *grid, splat.Get(), base, AABB::Empty(), unpainted);
    REQUIRE(!unpainted.transforms.IsEmpty());
    for (const Float4x4& m : unpainted.transforms)
    {
        CHECK(m.m[3][0] > 0.0f);
    }

    // A layer nobody painted, or a threshold above every share, grows nothing; nor does a
    // Splat layer with no splat at all.
    veg::VegetationLayer rocks = grass;
    rocks.splatLayer = 3;
    veg::ScatterResult none;
    veg::ScatterChunk(3, chunk, *grid, splat.Get(), rocks, AABB::Empty(), none);
    CHECK(none.transforms.IsEmpty());
    veg::VegetationLayer strict = grass;
    strict.splatThreshold = 1.5f;
    veg::ScatterChunk(3, chunk, *grid, splat.Get(), strict, AABB::Empty(), none);
    CHECK(none.transforms.IsEmpty());
    veg::ScatterChunk(3, chunk, *grid, nullptr, grass, AABB::Empty(), none);
    CHECK(none.transforms.IsEmpty());

    // The share is the sampled weight: a half-weight paint keeps about half the candidates.
    CHECK(veg::PlacementShareAt(grass, *grid, splat.Get(), -16.0f, 0.0f) == doctest::Approx(1.0f));
    CHECK(veg::PlacementShareAt(grass, *grid, splat.Get(), 16.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(veg::PlacementShareAt(base, *grid, splat.Get(), 16.0f, 0.0f) == doctest::Approx(1.0f));
}

TEST_CASE("vegetation scatter: the slope limit and the height window reject")
{
    // A ramp rising 32 m over 64 m: slope atan(0.5) = 26.6 degrees everywhere.
    RefPtr<hf::Heightfield> ramp = MakeRamp(32.0f);
    const tmodel::TerrainChunk chunk = ChunkOf(*ramp);

    veg::VegetationLayer gentle = Uniform(0.25f);
    // The limit sits under the ramp's slope everywhere - the central difference halves at the
    // clamped x edges (13.3 degrees there), so 10 keeps rejecting at the rim too.
    gentle.maxSlopeDegrees = 10.0f;
    veg::ScatterResult none;
    veg::ScatterChunk(11, chunk, *ramp, nullptr, gentle, AABB::Empty(), none);
    CHECK(none.transforms.IsEmpty());

    veg::VegetationLayer steep = Uniform(0.25f);
    steep.maxSlopeDegrees = 35.0f; // allowed
    veg::ScatterResult all;
    veg::ScatterChunk(11, chunk, *ramp, nullptr, steep, AABB::Empty(), all);
    CHECK(all.transforms.Size() == all.candidateCount);

    // The height window: only the band 8..16 m up the ramp (x in the middle quarter).
    veg::VegetationLayer band = steep;
    band.heightRange = Float2{8.0f, 16.0f};
    veg::ScatterResult banded;
    veg::ScatterChunk(11, chunk, *ramp, nullptr, band, AABB::Empty(), banded);
    REQUIRE(!banded.transforms.IsEmpty());
    CHECK(banded.transforms.Size() < all.transforms.Size());
    for (const Float4x4& m : banded.transforms)
    {
        CHECK(m.m[3][1] >= 8.0f - 0.05f);
        CHECK(m.m[3][1] <= 16.0f + 0.05f);
    }
}

TEST_CASE("vegetation scatter: alignToNormal tilts each instance onto the sampled normal; scale is uniform")
{
    RefPtr<hf::Heightfield> ramp = MakeRamp(32.0f);
    const tmodel::TerrainChunk chunk = ChunkOf(*ramp);
    const Float3 expected = Normalized(Float3{-0.5f, 1.0f, 0.0f}); // the ramp's normal

    veg::VegetationLayer flat = Uniform(0.05f);
    flat.scaleRange = Float2{2.0f, 2.0f};
    veg::ScatterResult upright;
    veg::ScatterChunk(5, chunk, *ramp, nullptr, flat, AABB::Empty(), upright);
    REQUIRE(!upright.transforms.IsEmpty());
    for (const Float4x4& m : upright.transforms)
    {
        const Float3 up = Row(m, 1);
        CHECK(Length(up) == doctest::Approx(2.0f).epsilon(0.001)); // the uniform scale
        CHECK(Normalized(up).y == doctest::Approx(1.0f).epsilon(0.001));
    }

    veg::VegetationLayer aligned = flat;
    aligned.alignToNormal = true;
    veg::ScatterResult tilted;
    veg::ScatterChunk(5, chunk, *ramp, nullptr, aligned, AABB::Empty(), tilted);
    REQUIRE(tilted.transforms.Size() == upright.transforms.Size()); // same candidates, same keeps
    for (const Float4x4& m : tilted.transforms)
    {
        const Float3 up = Normalized(Row(m, 1));
        if (Abs(m.m[3][0]) < 31.0f) // inside the rim the central difference is the true slope
        {
            CHECK(Dot(up, expected) == doctest::Approx(1.0f).epsilon(0.01));
        }
        // Still a right-handed orthonormal frame at scale 2.
        const Float3 x = Row(m, 0);
        const Float3 z = Row(m, 2);
        CHECK(Length(x) == doctest::Approx(2.0f).epsilon(0.001));
        CHECK(Dot(Normalized(x), up) == doctest::Approx(0.0f).epsilon(0.01));
        CHECK(Dot(Normalized(z), up) == doctest::Approx(0.0f).epsilon(0.01));
        CHECK(Dot(Cross(Normalized(x), up), Normalized(z)) == doctest::Approx(1.0f).epsilon(0.01));
    }
}

TEST_CASE("vegetation scatter: the mesh extent grows the chunk bounds by the largest scale")
{
    RefPtr<hf::Heightfield> grid = MakeFlat(3.0f);
    const tmodel::TerrainChunk chunk = ChunkOf(*grid);
    veg::VegetationLayer layer = Uniform(0.1f);
    layer.scaleRange = Float2{1.0f, 3.0f};
    const AABB blade = AABB::FromCenterExtents(Float3{0.0f, 0.5f, 0.0f}, Float3{0.1f, 0.5f, 0.1f});

    veg::ScatterResult r;
    veg::ScatterChunk(9, chunk, *grid, nullptr, layer, blade, r);
    REQUIRE(!r.transforms.IsEmpty());
    // reach = |extents| + |center| = sqrt(0.01 + 0.25 + 0.01) + 0.5 ~ 1.0198; x 3 = 3.06
    const f32 grow = (Length(Float3{0.1f, 0.5f, 0.1f}) + 0.5f) * 3.0f;
    CHECK(r.localBounds.min.x == doctest::Approx(chunk.bounds.min.x - grow));
    CHECK(r.localBounds.max.y == doctest::Approx(chunk.bounds.max.y + grow));

    veg::ScatterResult bare;
    veg::ScatterChunk(9, chunk, *grid, nullptr, layer, AABB::Empty(), bare);
    CHECK(bare.localBounds.min.x == doctest::Approx(chunk.bounds.min.x));
}

TEST_CASE("vegetation fade: full inside fadeStart, zero beyond fadeEnd, monotone between; the prefix is a count")
{
    CHECK(veg::DensityAtDistance(0.0f, 40.0f, 80.0f) == 1.0f);
    CHECK(veg::DensityAtDistance(40.0f, 40.0f, 80.0f) == 1.0f);
    CHECK(veg::DensityAtDistance(80.0f, 40.0f, 80.0f) == 0.0f);
    CHECK(veg::DensityAtDistance(500.0f, 40.0f, 80.0f) == 0.0f);
    CHECK(veg::DensityAtDistance(60.0f, 40.0f, 80.0f) == doctest::Approx(0.5f));
    f32 previous = 1.0f;
    for (f32 d = 40.0f; d <= 80.0f; d += 1.0f)
    {
        const f32 now = veg::DensityAtDistance(d, 40.0f, 80.0f);
        CHECK(now <= previous);
        previous = now;
    }
    // A degenerate window is a hard cut at fadeEnd.
    CHECK(veg::DensityAtDistance(79.0f, 80.0f, 80.0f) == 1.0f);
    CHECK(veg::DensityAtDistance(80.0f, 80.0f, 80.0f) == 0.0f);

    CHECK(veg::FadePrefix(1000, 1.0f) == 1000u);
    CHECK(veg::FadePrefix(1000, 0.0f) == 0u);
    CHECK(veg::FadePrefix(1000, 0.5f) == 500u);
    CHECK(veg::FadePrefix(1000, 2.0f) == 1000u);
    CHECK(veg::FadePrefix(3, 0.5f) == 2u); // rounds
    CHECK(veg::FadePrefix(0, 0.5f) == 0u);
}

TEST_CASE("vegetation: ChunksTouchedBy maps a grid region to the chunks that share its samples")
{
    Array<u32> touched;
    hf::HeightfieldRegion inside;
    inside.minX = 10;
    inside.maxX = 20;
    inside.minZ = 70;
    inside.maxZ = 80;
    veg::ChunksTouchedBy(inside, 4, touched); // a 257-sample grid: 4 x 4 chunks
    REQUIRE(touched.Size() == 1u);
    CHECK(touched[0] == 4u); // chunk (0, 1)

    // A region ending ON the shared boundary sample 64 touches chunk 0 and chunk 1.
    touched.Clear();
    hf::HeightfieldRegion edge;
    edge.minX = 60;
    edge.maxX = 64;
    edge.minZ = 5;
    edge.maxZ = 6;
    veg::ChunksTouchedBy(edge, 4, touched);
    REQUIRE(touched.Size() == 2u);
    CHECK(touched[0] == 0u);
    CHECK(touched[1] == 1u);

    // A region starting on a boundary sample touches the chunk before it too.
    touched.Clear();
    hf::HeightfieldRegion corner;
    corner.minX = 64;
    corner.maxX = 64;
    corner.minZ = 64;
    corner.maxZ = 64;
    veg::ChunksTouchedBy(corner, 4, touched);
    CHECK(touched.Size() == 4u);

    // Appends unique: a second call over the same region adds nothing; an empty region nothing.
    veg::ChunksTouchedBy(corner, 4, touched);
    CHECK(touched.Size() == 4u);
    veg::ChunksTouchedBy(hf::HeightfieldRegion{}, 4, touched);
    CHECK(touched.Size() == 4u);

    // Clamped to the grid.
    touched.Clear();
    hf::HeightfieldRegion beyond;
    beyond.minX = 250;
    beyond.maxX = 400;
    beyond.minZ = 0;
    beyond.maxZ = 0;
    veg::ChunksTouchedBy(beyond, 4, touched);
    REQUIRE(touched.Size() == 1u);
    CHECK(touched[0] == 3u);
}

TEST_CASE("vegetation layer: the scatter hash covers the scatter parameters, not the per-frame draw state")
{
    veg::VegetationLayer a;
    veg::VegetationLayer b = a;
    CHECK(veg::LayerScatterHash(a) == veg::LayerScatterHash(b));
    b.density = 3.0f;
    CHECK(veg::LayerScatterHash(a) != veg::LayerScatterHash(b));
    b = a;
    b.fadeEnd = 200.0f; // fade + shadows are draw-time: no regrow
    b.castShadows = true;
    CHECK(veg::LayerScatterHash(a) == veg::LayerScatterHash(b));
    for (const veg::VegetationPlacement p :
         {veg::VegetationPlacement::Uniform, veg::VegetationPlacement::Mask})
    {
        b = a;
        b.placement = p;
        CHECK(veg::LayerScatterHash(a) != veg::LayerScatterHash(b));
    }
}
