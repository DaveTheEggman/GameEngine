// Draconic GUI - Text tests: measurement and alignment logic against a mock IFont (6px
// advance/byte, 12px line height). The full glyph-render path (atlas + texture) is an
// integration concern; here we assert Text's own logic + the draw guards.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.fonts;
import draconic.vg;
import draconic.gui;

using namespace draconic::gui;
namespace core = draconic::core;
namespace fonts = draconic::fonts;
namespace vg = draconic::vg;

namespace
{
    // 6px advance per byte, 12px line height.
    class MockFont : public fonts::IFont
    {
    public:
        core::StringView FamilyName() const override { return core::StringView(u8"mock"); }
        fonts::FontMetrics Metrics() const override
        {
            fonts::FontMetrics m;
            m.ascent = 10.0f; m.descent = -2.0f; m.lineGap = 0.0f;
            m.lineHeight = 12.0f; m.pixelHeight = 10.0f; m.scale = 1.0f;
            return m;
        }
        core::f32 PixelHeight() const override { return 10.0f; }
        fonts::GlyphInfo GetGlyphInfo(core::i32 cp) const override
        {
            fonts::GlyphInfo g; g.codepoint = cp; g.advanceWidth = 6.0f; return g;
        }
        core::f32 GetKerning(core::i32, core::i32) const override { return 0.0f; }
        bool HasGlyph(core::i32) const override { return true; }
        core::f32 MeasureString(core::StringView text) const override { return static_cast<core::f32>(text.Size()) * 6.0f; }
        core::f32 MeasureString(core::StringView text, core::Array<fonts::GlyphPosition>& out) const override
        {
            (void)out; return static_cast<core::f32>(text.Size()) * 6.0f;
        }
    };

    MockFont* NewMock() { return core::DefaultAllocator().New<MockFont>(); }
}

TEST_CASE("text: measurement uses font metrics")
{
    fonts::CachedFont cf(NewMock(), nullptr, nullptr); // owns + deletes the mock on scope exit
    Text t{ core::StringView(u8"Hello"), &cf };
    CHECK(t.GetWidth() == doctest::Approx(30.0f));   // 5 * 6
    CHECK(t.GetLineHeight() == doctest::Approx(12.0f));
    CHECK(t.Measure().x == doctest::Approx(30.0f));
    CHECK(t.Measure().y == doctest::Approx(12.0f));
}

TEST_CASE("text: no font measures to zero")
{
    Text t;
    t.SetString(core::StringView(u8"Hello"));
    CHECK(t.GetWidth() == 0.0f);
    CHECK(t.GetLineHeight() == 0.0f);
}

TEST_CASE("text: string/color/alignment accessors")
{
    Text t;
    t.SetString(core::StringView(u8"abc"));
    CHECK(t.GetString() == core::StringView(u8"abc"));
    CHECK_FALSE(t.IsEmpty());
    t.SetColor(core::Color::Red);
    CHECK(t.GetColor().r == doctest::Approx(1.0f));
    t.SetAlignment(TextHAlign::Center, TextVAlign::Bottom);
    CHECK(t.GetHAlign() == TextHAlign::Center);
    CHECK(t.GetVAlign() == TextVAlign::Bottom);
}

TEST_CASE("text: alignment within bounds")
{
    fonts::CachedFont cf(NewMock(), nullptr, nullptr);
    Text t{ core::StringView(u8"Hello"), &cf }; // width 30, height 12
    const Rect bounds{ 0.0f, 0.0f, 100.0f, 50.0f };

    t.SetAlignment(TextHAlign::Left, TextVAlign::Top);
    CHECK(t.AlignedPosition(bounds).x == doctest::Approx(0.0f));
    CHECK(t.AlignedPosition(bounds).y == doctest::Approx(0.0f));

    t.SetAlignment(TextHAlign::Center, TextVAlign::Middle);
    CHECK(t.AlignedPosition(bounds).x == doctest::Approx(35.0f)); // (100-30)/2
    CHECK(t.AlignedPosition(bounds).y == doctest::Approx(19.0f)); // (50-12)/2

    t.SetAlignment(TextHAlign::Right, TextVAlign::Bottom);
    CHECK(t.AlignedPosition(bounds).x == doctest::Approx(70.0f)); // 100-30
    CHECK(t.AlignedPosition(bounds).y == doctest::Approx(38.0f)); // 50-12
}

TEST_CASE("text: draw guards - no font or empty produces no geometry")
{
    vg::VGContext ctx; // no font service -> VG DrawText early-returns anyway
    DrawContext dc{ ctx };

    Text noFont;
    noFont.SetString(core::StringView(u8"Hello"));
    noFont.Draw(dc, core::Float2{ 0.0f, 0.0f });
    CHECK(ctx.GetBatch().vertices.Size() == 0);

    fonts::CachedFont cf(NewMock(), nullptr, nullptr);
    Text empty{ core::StringView(u8""), &cf };
    empty.Draw(dc, core::Float2{ 0.0f, 0.0f });
    CHECK(ctx.GetBatch().vertices.Size() == 0);
}
