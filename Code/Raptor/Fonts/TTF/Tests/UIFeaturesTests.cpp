// Ported from Sedulous.Fonts.Tests/UIFeaturesTests.bf — the font-driven shaper
// UI helpers (hit testing, cursor position, selection rects, font decoration
// metrics) over the bundled Roboto asset. The pure value-type cases live in
// the core types suite.
#include <doctest/doctest.h>

#include "Core/Prelude.h"

import raptor.core;
import raptor.fonts;
import raptor.fonts.io;
import raptor.fonts.ttf;

using namespace raptor::core;
using namespace raptor::fonts;

namespace
{
    WideString AssetPath(const char* rel)
    {
        WideString p;
        for (const char* s = RAPTOR_FONTS_ASSET_DIR; *s != '\0'; ++s) p.PushBack(static_cast<widechar>(static_cast<unsigned char>(*s)));
        for (const char* s = rel; *s != '\0'; ++s) p.PushBack(static_cast<widechar>(static_cast<unsigned char>(*s)));
        return p;
    }

    IFont* LoadRoboto()
    {
        const WideString path = AssetPath("/roboto/Roboto-Regular.ttf");
        Result<IFont*, FontLoadResult> parsed = FontParserFactory::ParseFromFile(path, FontLoadOptions::Default());
        return parsed.HasValue() ? parsed.Value() : nullptr;
    }

    Span<const GlyphPosition> AsSpan(const Array<GlyphPosition>& a)
    {
        return Span<const GlyphPosition>(a.Data(), a.Size());
    }
}

TEST_CASE("ttf.ui: HitTest on empty positions")
{
    TrueTypeFonts::Initialize();
    IFont* font = LoadRoboto();
    REQUIRE(font != nullptr);

    TrueTypeTextShaper shaper;
    Array<GlyphPosition> positions;
    const HitTestResult result = shaper.HitTest(*font, AsSpan(positions), 50, 0);
    CHECK(result.characterIndex == 0);
    CHECK_FALSE(result.isInside);

    DefaultAllocator().Delete(font);
    TrueTypeFonts::Shutdown();
}

TEST_CASE("ttf.ui: HitTest before text")
{
    TrueTypeFonts::Initialize();
    IFont* font = LoadRoboto();
    REQUIRE(font != nullptr);

    TrueTypeTextShaper shaper;
    Array<GlyphPosition> positions;
    (void)shaper.ShapeText(*font, u"Hello", 100, 0, positions);

    const HitTestResult result = shaper.HitTest(*font, AsSpan(positions), 50, 0);
    CHECK(result.characterIndex == 0);
    CHECK_FALSE(result.isTrailingHit);

    DefaultAllocator().Delete(font);
    TrueTypeFonts::Shutdown();
}

TEST_CASE("ttf.ui: HitTest middle of a character")
{
    TrueTypeFonts::Initialize();
    IFont* font = LoadRoboto();
    REQUIRE(font != nullptr);

    TrueTypeTextShaper shaper;
    Array<GlyphPosition> positions;
    (void)shaper.ShapeText(*font, u"ABCDE", positions);
    REQUIRE(positions.Size() >= 3);

    const GlyphPosition& pos = positions[2];
    const f32 midX = pos.x + pos.advance * 0.5f;
    const HitTestResult result = shaper.HitTest(*font, AsSpan(positions), midX, 0);
    CHECK(result.characterIndex == 2);
    CHECK(result.isInside);

    DefaultAllocator().Delete(font);
    TrueTypeFonts::Shutdown();
}

TEST_CASE("ttf.ui: HitTest trailing edge")
{
    TrueTypeFonts::Initialize();
    IFont* font = LoadRoboto();
    REQUIRE(font != nullptr);

    TrueTypeTextShaper shaper;
    Array<GlyphPosition> positions;
    (void)shaper.ShapeText(*font, u"AB", positions);
    REQUIRE(positions.Size() >= 1);

    const GlyphPosition& pos = positions[0];
    const f32 trailingX = pos.x + pos.advance * 0.75f;
    const HitTestResult result = shaper.HitTest(*font, AsSpan(positions), trailingX, 0);
    CHECK(result.characterIndex == 0);
    CHECK(result.isTrailingHit);
    CHECK(result.InsertionIndex() == 1);

    DefaultAllocator().Delete(font);
    TrueTypeFonts::Shutdown();
}

TEST_CASE("ttf.ui: GetCursorPosition at start")
{
    TrueTypeFonts::Initialize();
    IFont* font = LoadRoboto();
    REQUIRE(font != nullptr);

    TrueTypeTextShaper shaper;
    Array<GlyphPosition> positions;
    (void)shaper.ShapeText(*font, u"Hello", 100, 0, positions);
    CHECK(shaper.GetCursorPosition(*font, AsSpan(positions), 0) == 100);

    DefaultAllocator().Delete(font);
    TrueTypeFonts::Shutdown();
}

TEST_CASE("ttf.ui: GetCursorPosition at end")
{
    TrueTypeFonts::Initialize();
    IFont* font = LoadRoboto();
    REQUIRE(font != nullptr);

    TrueTypeTextShaper shaper;
    Array<GlyphPosition> positions;
    (void)shaper.ShapeText(*font, u"Hi", 0, 0, positions);
    REQUIRE(positions.Size() >= 2);

    const f32 cursorX = shaper.GetCursorPosition(*font, AsSpan(positions), static_cast<i32>(positions.Size()));
    const GlyphPosition& last = positions[positions.Size() - 1];
    CHECK(Abs(cursorX - (last.x + last.advance)) < 0.01f);

    DefaultAllocator().Delete(font);
    TrueTypeFonts::Shutdown();
}

TEST_CASE("ttf.ui: GetCursorPosition in the middle")
{
    TrueTypeFonts::Initialize();
    IFont* font = LoadRoboto();
    REQUIRE(font != nullptr);

    TrueTypeTextShaper shaper;
    Array<GlyphPosition> positions;
    (void)shaper.ShapeText(*font, u"ABC", positions);
    REQUIRE(positions.Size() >= 3);
    CHECK(shaper.GetCursorPosition(*font, AsSpan(positions), 1) == positions[1].x);

    DefaultAllocator().Delete(font);
    TrueTypeFonts::Shutdown();
}

TEST_CASE("ttf.ui: GetSelectionRects empty selection")
{
    TrueTypeFonts::Initialize();
    IFont* font = LoadRoboto();
    REQUIRE(font != nullptr);

    TrueTypeTextShaper shaper;
    Array<GlyphPosition> positions;
    (void)shaper.ShapeText(*font, u"Hello", positions);

    Array<raptor::fonts::Rect> rects;
    shaper.GetSelectionRects(*font, AsSpan(positions), SelectionRange(2, 2), font->Metrics().lineHeight, rects);
    CHECK(rects.Size() == 0);

    DefaultAllocator().Delete(font);
    TrueTypeFonts::Shutdown();
}

TEST_CASE("ttf.ui: GetSelectionRects single line")
{
    TrueTypeFonts::Initialize();
    IFont* font = LoadRoboto();
    REQUIRE(font != nullptr);

    TrueTypeTextShaper shaper;
    Array<GlyphPosition> positions;
    (void)shaper.ShapeText(*font, u"Hello", positions);

    Array<raptor::fonts::Rect> rects;
    shaper.GetSelectionRects(*font, AsSpan(positions), SelectionRange(1, 4), font->Metrics().lineHeight, rects);
    REQUIRE(rects.Size() == 1);
    CHECK(rects[0].width > 0);
    CHECK(rects[0].height > 0);

    DefaultAllocator().Delete(font);
    TrueTypeFonts::Shutdown();
}

TEST_CASE("ttf.ui: font metrics carry decoration metrics")
{
    TrueTypeFonts::Initialize();
    IFont* font = LoadRoboto();
    REQUIRE(font != nullptr);

    const TextDecorationMetrics decorations = font->Metrics().decorations;
    CHECK(decorations.underlineThickness >= 1);
    CHECK(decorations.strikethroughThickness >= 1);
    CHECK(decorations.underlinePosition > 0);
    CHECK(decorations.strikethroughPosition < 0);

    DefaultAllocator().Delete(font);
    TrueTypeFonts::Shutdown();
}
