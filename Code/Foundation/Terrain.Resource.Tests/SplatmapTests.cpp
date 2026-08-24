// The editable terrain Splatmap: the pure weight-brush core (PaintWeight - lerp-to-one-hot with a
// cosine falloff, region + version tracking) and the SplatmapSource metadata + pixel-blob round-trip
// the cook rides. RHI-free, no content DB - the runtime factory round-trip lives in the Pipeline test.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.terrain.resource;

using namespace foundation::core;
using namespace foundation::terrain;

TEST_CASE("splatmap: a fresh raster is all-zero; SeedLayer0 sets the base channel")
{
    Splatmap sm(8, 8);
    CHECK_FALSE(sm.IsEmpty());
    CHECK(sm.Width() == 8);
    CHECK(sm.Height() == 8);
    CHECK(sm.Version() == 1u);
    CHECK(sm.GetWeight(4, 4, 0) == 0);

    sm.SeedLayer0();
    CHECK(sm.GetWeight(0, 0, 0) == 255);
    CHECK(sm.GetWeight(7, 7, 0) == 255);
    CHECK(sm.GetWeight(4, 4, 1) == 0);
}

TEST_CASE("splatmap: PaintWeight raises the selected layer at the centre, bumps version, reports region")
{
    Splatmap sm(64, 64);
    sm.SeedLayer0(); // start all layer 0
    const u64 v0 = sm.Version();

    // Paint layer 1 at the centre (uv 0.5), radius 0.2, full strength.
    const SplatRegion r = PaintWeight(sm, 0.5f, 0.5f, 0.2f, 1u, 1.0f);

    REQUIRE_FALSE(r.IsEmpty());
    CHECK(sm.Version() > v0); // an edit bumped the version (GPU re-upload)

    // Centre texel is now dominated by layer 1, and layer 0 was eroded (lerp-to-one-hot).
    const u8 c1 = sm.GetWeight(32, 32, 1);
    const u8 c0 = sm.GetWeight(32, 32, 0);
    CHECK(c1 > 200);
    CHECK(c0 < 55);

    // A rim texel inside the disc is touched but less (cosine falloff): more layer 0 than the centre.
    CHECK(sm.GetWeight(32, 32, 1) >= sm.GetWeight(r.minX, 32, 1));

    // The region is a rect around the centre, well within the raster.
    CHECK(r.minX >= 0);
    CHECK(r.maxX <= 63);
    CHECK(r.minX < 32);
    CHECK(r.maxX > 32);
}

TEST_CASE("splatmap: painting another layer erases the previous one (lerp-to-one-hot)")
{
    Splatmap sm(32, 32);
    sm.SeedLayer0();
    (void)PaintWeight(sm, 0.5f, 0.5f, 0.4f, 2u, 1.0f); // paint layer 2 over the centre
    CHECK(sm.GetWeight(16, 16, 2) > 200);
    (void)PaintWeight(sm, 0.5f, 0.5f, 0.4f, 3u, 1.0f); // now paint layer 3 - erases layer 2
    CHECK(sm.GetWeight(16, 16, 3) > 200);
    CHECK(sm.GetWeight(16, 16, 2) < 55);
}

TEST_CASE("splatmap: an off-raster / zero-radius brush touches nothing and does not bump")
{
    Splatmap sm(16, 16);
    const u64 v0 = sm.Version();
    const SplatRegion a = PaintWeight(sm, 5.0f, 5.0f, 0.1f, 0u, 1.0f); // uv way outside 0..1
    CHECK(a.IsEmpty());
    const SplatRegion b = PaintWeight(sm, 0.5f, 0.5f, 0.0f, 0u, 1.0f); // zero radius
    CHECK(b.IsEmpty());
    CHECK(sm.Version() == v0); // nothing changed
}

TEST_CASE("splatmap: SplatmapSource metadata + pixel blob round-trips an identical raster")
{
    Splatmap sm(24, 16);
    sm.SeedLayer0();
    (void)PaintWeight(sm, 0.3f, 0.7f, 0.25f, 1u, 0.8f);
    (void)PaintWeight(sm, 0.8f, 0.2f, 0.15f, 2u, 1.0f);

    SplatmapSource src;
    SplatmapSource::FromSplatmap(sm, src);
    CHECK(src.width == 24);
    CHECK(src.height == 16);

    const Span<const byte> blob = SplatmapSource::PixelBlob(sm);
    RefPtr<Splatmap> rebuilt = src.Build(blob);
    REQUIRE(rebuilt);
    CHECK(rebuilt->Width() == 24);
    CHECK(rebuilt->Height() == 16);
    // Every texel identical.
    bool identical = true;
    const Span<const u8> a = sm.Pixels();
    const Span<const u8> b = rebuilt->Pixels();
    REQUIRE(a.Size() == b.Size());
    for (usize i = 0; i < a.Size(); ++i)
    {
        if (a[i] != b[i])
        {
            identical = false;
            break;
        }
    }
    CHECK(identical);

    // A blob whose size does not match the metadata yields an empty (safe) raster.
    RefPtr<Splatmap> bad = src.Build(Span<const byte>{blob.Data(), blob.Size() - 4});
    REQUIRE(bad);
    CHECK(bad->IsEmpty());
}
