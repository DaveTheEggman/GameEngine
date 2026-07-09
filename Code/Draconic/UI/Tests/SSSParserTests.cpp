// Ported from Sedulous.UI.Tests/src/SSSParserTests.bf.
//
// SCOPE: The great majority of the Sedulous SSSParserTests are INTEGRATION tests that require the
// not-yet-ported View cluster + controls (UIContext, RootView, TestView/TestGroup, Button, CheckBox,
// and view.ResolveStyleColor/Float/Thickness/Drawable/ResolvePartDrawable). Those are DEFERRED until
// the View cluster lands, together with the already-deferred StyleSheetTests/InlineStyleTests/etc.
// This file ports the View-INDEPENDENT subset that exercises the tokenize->parse->StyleSheet pipeline
// directly: pure value parsers, palette derivation, and parse-then-inspect (rule/property counts,
// selector state). These register "View" explicitly since UITypeRegistry::RegisterBuiltins is deferred.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;

using namespace draconic::ui;
namespace core = draconic::core;
using namespace draconic::core;

// Register the drawable factories + the one View type these parser tests reference.
static void EnsureGlobals()
{
    StyleSheetLoader::InitializeGlobals();
    UITypeRegistry::Register(u8"View", &View::StaticType());
}

// === Hex color parsing (StyleValueParser) ===

TEST_CASE("sss: HexColor_6Digit")
{
    core::Optional<Color> c = StyleValueParser::ParseHexColor(u8"#4a8eff");
    REQUIRE(c.HasValue());
    CHECK(c.Value().r == doctest::Approx(0x4a / 255.0f));
    CHECK(c.Value().g == doctest::Approx(0x8e / 255.0f));
    CHECK(c.Value().b == 1.0f);
    CHECK(c.Value().a == 1.0f);
}

TEST_CASE("sss: HexColor_8Digit")
{
    core::Optional<Color> c = StyleValueParser::ParseHexColor(u8"#4a8effcc");
    REQUIRE(c.HasValue());
    CHECK(c.Value().r == doctest::Approx(0x4a / 255.0f));
    CHECK(c.Value().a == doctest::Approx(0xcc / 255.0f));
}

// === Palette color derivation ===

TEST_CASE("sss: Palette_Derivation")
{
    const Color dark = Palette::Darken(Color{ 200 / 255.0f, 200 / 255.0f, 200 / 255.0f, 1.0f }, 0.5f);
    CHECK(dark.r == doctest::Approx(100 / 255.0f));

    const Color light = Palette::Lighten(Color{ 0.0f, 0.0f, 0.0f, 1.0f }, 0.5f);
    CHECK(light.r > 100 / 255.0f);
}

// === Parse-then-inspect (no View instance) ===

TEST_CASE("sss: CompoundStateRule")
{
    EnsureGlobals();
    StyleSheetLoader loader;
    core::RefPtr<StyleSheet> sheet = loader.Load(u8R"(
        View { text-color: #ffffff; }
        View:checked:hover { text-color: #ff0000; }
    )");

    CHECK(sheet->RuleCount() == 2);

    // Verify the selector has compound state flags.
    const StyleRule& rule = sheet->GetRule(1);
    REQUIRE(rule.Selector.State.HasValue());
    const ControlState state = rule.Selector.State.Value();
    CHECK(HasFlag(state, ControlState::Checked));
    CHECK(HasFlag(state, ControlState::Hover));
}

TEST_CASE("sss: AllDrawableProperties_Parse")
{
    EnsureGlobals();
    StyleSheetLoader loader;
    core::RefPtr<StyleSheet> sheet = loader.Load(u8R"(
        View {
            background: color(#111);
            checked-background: color(#222);
            menu-item-hover-drawable: color(#333);
        }
    )");

    CHECK(sheet->RuleCount() == 1);
    CHECK(sheet->GetRule(0).PropertyCount() == 3);
}

TEST_CASE("sss: AllColorProperties_Parse")
{
    EnsureGlobals();
    StyleSheetLoader loader;
    core::RefPtr<StyleSheet> sheet = loader.Load(u8R"(
        View {
            text-color: #111;
            text-dim-color: #222;
            placeholder-color: #333;
            border-color: #444;
            cursor-color: #555;
            selection-color: #666;
            accent-color: #777;
        }
    )");

    CHECK(sheet->GetRule(0).PropertyCount() == 7);
}

TEST_CASE("sss: AllFloatProperties_Parse")
{
    EnsureGlobals();
    StyleSheetLoader loader;
    core::RefPtr<StyleSheet> sheet = loader.Load(u8R"(
        View {
            font-size: 16;
            corner-radius: 4;
            border-width: 1;
            spacing: 8;
            opacity: 0.5;
            width: 100;
            height: 50;
        }
    )");

    CHECK(sheet->GetRule(0).PropertyCount() == 7);
}

TEST_CASE("sss: Comments_Ignored")
{
    EnsureGlobals();
    StyleSheetLoader loader;
    core::RefPtr<StyleSheet> sheet = loader.Load(u8R"(
        /* This is a comment */
        View {
            font-size: 14; /* inline comment */
        }
    )");

    CHECK(sheet->RuleCount() == 1);
}
