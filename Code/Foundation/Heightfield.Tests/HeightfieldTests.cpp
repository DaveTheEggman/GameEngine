// foundation.heightfield: the pure grid + sampling math. Covers the size contract, exact grid-point
// and bilinear-midpoint sampling, edge clamp, world<->grid round-trip, normals, cell bounds, and the
// ray-march query (the editor-brush / physics-shared height source). Heightfield IS-A Object (a
// resource product), so it is held through a RefPtr.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.heightfield;

using namespace foundation::core;
using namespace foundation::heightfield;

namespace
{
    // A flat-then-ramped fixture: a 65-grid over a 64x64 world, Y range [0, 10], with sample values
    // that increase linearly along +X (so world height is a known ramp).
    RefPtr<Heightfield> MakeRampX()
    {
        RefPtr<Heightfield> hf = MakeRef<Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f},
                                                      0.0f, 10.0f);
        for (i32 z = 0; z < 65; ++z)
        {
            for (i32 x = 0; x < 65; ++x)
            {
                // sample increases with x: 0 at x=0, 65535 at x=64.
                const f32 t = static_cast<f32>(x) / 64.0f;
                hf->SetSample(x, z, static_cast<Height>(t * 65535.0f + 0.5f));
            }
        }
        return hf;
    }

    bool Near(f32 a, f32 b, f32 eps = 1.0e-2f) { return Abs(a - b) <= eps; }
}

TEST_CASE("heightfield: size contract - square 64k+1 only")
{
    CHECK(IsValidSize(65));
    CHECK(IsValidSize(129));
    CHECK(IsValidSize(257));
    CHECK(IsValidSize(1025));
    CHECK_FALSE(IsValidSize(64));  // one short
    CHECK_FALSE(IsValidSize(66));  // not 64k+1
    CHECK_FALSE(IsValidSize(1));   // too small
    CHECK_FALSE(IsValidSize(129 - 1));

    CHECK(NextValidSize(1) == 65);
    CHECK(NextValidSize(65) == 65);
    CHECK(NextValidSize(66) == 129);
    CHECK(NextValidSize(129) == 129);
    CHECK(NextValidSize(200) == 257);
}

TEST_CASE("heightfield: empty default is inert")
{
    RefPtr<Heightfield> owner = MakeRef<Heightfield>(DefaultAllocator());
    const Heightfield& hf = *owner;
    CHECK(hf.IsEmpty());
    CHECK(hf.Size() == 0);
    CHECK(hf.GetHeightAt(0.0f, 0.0f) == 0.0f);
}

TEST_CASE("heightfield: construction + raw sample round-trip")
{
    RefPtr<Heightfield> owner =
        MakeRef<Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, -5.0f, 5.0f);
    Heightfield& hf = *owner;
    CHECK_FALSE(hf.IsEmpty());
    CHECK(hf.Size() == 65);
    CHECK(hf.Samples().Size() == 65u * 65u);
    CHECK(hf.MinY() == -5.0f);
    CHECK(hf.MaxY() == 5.0f);

    hf.SetSample(3, 7, 40000);
    CHECK(hf.GetSample(3, 7) == 40000);
    // Untouched neighbours stay zero.
    CHECK(hf.GetSample(4, 7) == 0);
}

TEST_CASE("heightfield: quantization maps u16 across the world Y range")
{
    RefPtr<Heightfield> owner =
        MakeRef<Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
    const Heightfield& hf = *owner;
    CHECK(Near(hf.SampleToWorldY(0.0f), 0.0f));
    CHECK(Near(hf.SampleToWorldY(65535.0f), 10.0f));
    CHECK(Near(hf.SampleToWorldY(32767.5f), 5.0f));

    CHECK(hf.WorldYToSample(0.0f) == 0);
    CHECK(hf.WorldYToSample(10.0f) == 65535);
    // Clamps out-of-range.
    CHECK(hf.WorldYToSample(-1.0f) == 0);
    CHECK(hf.WorldYToSample(99.0f) == 65535);
}

TEST_CASE("heightfield: exact grid-point heights")
{
    RefPtr<Heightfield> owner = MakeRampX();
    const Heightfield& hf = *owner;
    // At x=0 (world -32), height 0; at x=64 (world +32), height 10.
    CHECK(Near(hf.GetHeightAtGrid(0, 0), 0.0f));
    CHECK(Near(hf.GetHeightAtGrid(64, 0), 10.0f));
    CHECK(Near(hf.GetHeightAtGrid(32, 0), 5.0f));
}

TEST_CASE("heightfield: bilinear midpoint sampling")
{
    RefPtr<Heightfield> owner = MakeRampX();
    const Heightfield& hf = *owner;
    // World x=0 is the grid centre (x=32) -> ~5. A midpoint between two columns interpolates.
    CHECK(Near(hf.GetHeightAt(0.0f, 0.0f), 5.0f));
    CHECK(Near(hf.GetHeightAt(-32.0f, 0.0f), 0.0f));
    CHECK(Near(hf.GetHeightAt(32.0f, 0.0f), 10.0f));
    // Halfway between world x=-32 and x=0 is x=-16 -> ~2.5.
    CHECK(Near(hf.GetHeightAt(-16.0f, 0.0f), 2.5f));
}

TEST_CASE("heightfield: sampling clamps at the edges")
{
    RefPtr<Heightfield> owner = MakeRampX();
    const Heightfield& hf = *owner;
    // Far outside the footprint clamps to the nearest edge column.
    CHECK(Near(hf.GetHeightAt(-1000.0f, 0.0f), 0.0f));
    CHECK(Near(hf.GetHeightAt(1000.0f, 0.0f), 10.0f));
}

TEST_CASE("heightfield: world<->grid round-trip")
{
    RefPtr<Heightfield> owner = MakeRampX();
    const Heightfield& hf = *owner;
    const f32 samples[] = {-32.0f, -10.0f, 0.0f, 15.5f, 32.0f};
    for (f32 wx : samples)
    {
        const Float2 g = hf.WorldToGrid(wx, wx);
        const Float2 w = hf.GridToWorld(g.x, g.y);
        CHECK(Near(w.x, wx, 1.0e-3f));
        CHECK(Near(w.y, wx, 1.0e-3f));
    }
    // Corners map to grid extremes.
    const Float2 g0 = hf.WorldToGrid(-32.0f, -32.0f);
    CHECK(Near(g0.x, 0.0f));
    CHECK(Near(g0.y, 0.0f));
    const Float2 g1 = hf.WorldToGrid(32.0f, 32.0f);
    CHECK(Near(g1.x, 64.0f));
    CHECK(Near(g1.y, 64.0f));
}

TEST_CASE("heightfield: flat field has an up normal, a ramp tilts")
{
    RefPtr<Heightfield> flatOwner =
        MakeRef<Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
    Heightfield& flat = *flatOwner;
    for (i32 i = 0; i < flat.Size() * flat.Size(); ++i)
    {
        flat.Samples()[static_cast<usize>(i)] = 30000;
    }
    const Float3 up = flat.GetNormalAt(0.0f, 0.0f);
    CHECK(Near(up.x, 0.0f));
    CHECK(Near(up.y, 1.0f));
    CHECK(Near(up.z, 0.0f));

    RefPtr<Heightfield> rampOwner = MakeRampX();
    const Float3 n = rampOwner->GetNormalAt(0.0f, 0.0f);
    CHECK(n.x < 0.0f);        // surface rises toward +X, so the normal tilts toward -X
    CHECK(n.y > 0.0f);
    CHECK(Near(n.z, 0.0f));
}

TEST_CASE("heightfield: cell bounds report the min/max over a block")
{
    RefPtr<Heightfield> owner = MakeRampX();
    const Heightfield& hf = *owner;
    f32 lo = 0.0f;
    f32 hi = 0.0f;
    hf.CellBounds(0, 0, 64, 64, lo, hi); // whole grid
    CHECK(Near(lo, 0.0f));
    CHECK(Near(hi, 10.0f));

    hf.CellBounds(0, 0, 0, 64, lo, hi); // first column only -> all 0
    CHECK(Near(lo, 0.0f));
    CHECK(Near(hi, 0.0f));

    hf.CellBounds(64, 0, 64, 64, lo, hi); // last column only -> all 10
    CHECK(Near(lo, 10.0f));
    CHECK(Near(hi, 10.0f));
}

TEST_CASE("heightfield: ray march - straight-down ray hits the surface height")
{
    RefPtr<Heightfield> owner = MakeRampX();
    const Heightfield& hf = *owner;
    // Drop from high above world (0,0): surface there is ~5.
    f32 t = 0.0f;
    REQUIRE(hf.QueryRay(Float3{0.0f, 100.0f, 0.0f}, Float3{0.0f, -1.0f, 0.0f}, t));
    const f32 hitY = 100.0f - t; // dir is -Y unit
    CHECK(Near(hitY, 5.0f, 5.0e-2f));

    // Over the tall edge (world +32) the surface is ~10.
    REQUIRE(hf.QueryRay(Float3{31.9f, 100.0f, 0.0f}, Float3{0.0f, -1.0f, 0.0f}, t));
    CHECK(Near(100.0f - t, 10.0f, 1.0e-1f));
}

TEST_CASE("heightfield: ray march - a ray that misses the footprint returns false")
{
    RefPtr<Heightfield> owner = MakeRampX();
    const Heightfield& hf = *owner;
    f32 t = 0.0f;
    // Straight down but far outside the XZ footprint.
    CHECK_FALSE(hf.QueryRay(Float3{1000.0f, 100.0f, 0.0f}, Float3{0.0f, -1.0f, 0.0f}, t));
    // Just outside the authored extent (footprint is [-32, 32]) also misses.
    CHECK_FALSE(hf.QueryRay(Float3{32.5f, 100.0f, 0.0f}, Float3{0.0f, -1.0f, 0.0f}, t));
    // Pointing up, away from the surface, from above.
    CHECK_FALSE(hf.QueryRay(Float3{0.0f, 100.0f, 0.0f}, Float3{0.0f, 1.0f, 0.0f}, t));
}

TEST_CASE("heightfield: ray march - an angled ray descends onto the ramp")
{
    RefPtr<Heightfield> owner = MakeRampX();
    const Heightfield& hf = *owner;
    // From above the low edge, angled toward +X and down; must land on the surface.
    f32 t = 0.0f;
    REQUIRE(hf.QueryRay(Float3{-30.0f, 20.0f, 0.0f}, Float3{1.0f, -1.0f, 0.0f}, t));
    const Float3 hit{-30.0f + 0.70710678f * t, 20.0f - 0.70710678f * t, 0.0f};
    CHECK(Near(hit.y, hf.GetHeightAt(hit.x, hit.z), 5.0e-2f));
}
