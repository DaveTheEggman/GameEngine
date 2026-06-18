// Ported from Sedulous.Fonts.Tests: Rect/AtlasRegion/GlyphInfo/FontMetrics/
// FontLoadOptions tests (the GPU/TTF-free type coverage).
#include <doctest/doctest.h>

#include "Core/Prelude.h"

import raptor.core;
import raptor.fonts;

using namespace raptor::core;
using namespace raptor::fonts;

TEST_CASE("fonts.rect: construction, bounds, contains, FromBounds")
{
    const raptor::fonts::Rect def;
    CHECK(def.x == 0); CHECK(def.y == 0); CHECK(def.width == 0); CHECK(def.height == 0);
    CHECK(def.IsEmpty());

    const raptor::fonts::Rect r(10, 20, 100, 50);
    CHECK(r.x == 10); CHECK(r.width == 100);
    CHECK_FALSE(r.IsEmpty());
    CHECK(r.Left() == 10); CHECK(r.Top() == 20); CHECK(r.Right() == 110); CHECK(r.Bottom() == 70);

    CHECK(r.Contains(50, 40));
    CHECK(r.Contains(10, 20));        // top-left inclusive
    CHECK_FALSE(r.Contains(110, 70)); // bottom-right exclusive
    CHECK_FALSE(r.Contains(5, 40));
    CHECK_FALSE(r.Contains(50, 100));

    const raptor::fonts::Rect b = raptor::fonts::Rect::FromBounds(10, 20, 110, 70);
    CHECK(b.x == 10); CHECK(b.y == 20); CHECK(b.width == 100); CHECK(b.height == 50);
}

TEST_CASE("fonts.atlasRegion: construction + UVs")
{
    const AtlasRegion def;
    CHECK(def.width == 0); CHECK(def.advanceX == 0); CHECK(def.IsEmpty());

    const AtlasRegion region(10, 20, 32, 48, 2.0f, -5.0f, 30.0f);
    CHECK(region.x == 10); CHECK(region.height == 48);
    CHECK(region.offsetY == -5.0f); CHECK(region.advanceX == 30.0f);
    CHECK_FALSE(region.IsEmpty());

    const AtlasRegion uvRegion(64, 128, 32, 48, 0, 0, 0);
    f32 u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    uvRegion.GetUVs(512, 512, u0, v0, u1, v1);
    CHECK(Abs(u0 - 0.125f) < 0.0001f);
    CHECK(Abs(v0 - 0.25f) < 0.0001f);
    CHECK(Abs(u1 - 0.1875f) < 0.0001f);
    CHECK(Abs(v1 - 0.34375f) < 0.0001f);
}

TEST_CASE("fonts.glyph: GlyphInfo defaults + GlyphQuad dimensions")
{
    const GlyphInfo info;
    CHECK(info.codepoint == 0); CHECK(info.glyphIndex == 0);
    CHECK(info.advanceWidth == 0); CHECK(info.leftSideBearing == 0);
    CHECK_FALSE(info.hasBitmap);

    const GlyphQuad quad(10, 20, 30, 50, 0, 0, 1, 1);
    CHECK(quad.Width() == 20);
    CHECK(quad.Height() == 30);

    const GlyphQuad def;
    CHECK(def.x0 == 0); CHECK(def.Width() == 0); CHECK(def.Height() == 0);
}

TEST_CASE("fonts.metrics: construction, line height, default")
{
    const FontMetrics m(20.0f, -5.0f, 2.0f, 24.0f, 0.5f);
    CHECK(m.ascent == 20.0f); CHECK(m.descent == -5.0f); CHECK(m.lineGap == 2.0f);
    CHECK(m.pixelHeight == 24.0f); CHECK(m.scale == 0.5f);
    CHECK(m.lineHeight == 27.0f);   // 20 - (-5) + 2

    const FontMetrics d = FontMetrics::Default();
    CHECK(d.ascent == 0); CHECK(d.lineHeight == 0); CHECK(d.scale == 1.0f);
}

TEST_CASE("fonts.loadOptions: presets + character count")
{
    auto isPow2 = [](u32 v) { return v > 0 && (v & (v - 1)) == 0; };

    const FontLoadOptions def = FontLoadOptions::Default();
    CHECK(def.pixelHeight > 0);
    CHECK(def.firstCodepoint >= 32);
    CHECK(def.lastCodepoint >= def.firstCodepoint);
    CHECK((def.atlasWidth > 0 && isPow2(def.atlasWidth)));
    CHECK((def.atlasHeight > 0 && isPow2(def.atlasHeight)));
    CHECK(def.oversampleX >= 1);
    CHECK(def.CharacterCount() == 95);   // 32..126

    const FontLoadOptions ext = FontLoadOptions::ExtendedLatin();
    CHECK(ext.lastCodepoint >= 255);
    CHECK(ext.atlasWidth >= 512);

    const FontLoadOptions small = FontLoadOptions::Small();
    CHECK(small.pixelHeight == 16.0f);
    CHECK(small.atlasWidth == 256);

    const FontLoadOptions large = FontLoadOptions::Large();
    CHECK(large.pixelHeight == 64.0f);
    CHECK(large.atlasWidth >= 1024);
}
