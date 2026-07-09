// Draconic GUI - CSS value parsers + typed property application: parse value strings into
// Color/length/bool/Thickness, and apply a resolved (or parsed) stylesheet onto a widget.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.gui;

using namespace draconic::gui;
namespace core = draconic::core;

namespace
{
    template <typename T> core::RefPtr<T> Make() { return core::MakeRef<T>(core::DefaultAllocator()); }
    core::StringView SV(const char8_t* s) { return core::StringView(s); }

    core::RefPtr<UIWidget> Widget(const char8_t* tag)
    {
        auto w = Make<UIWidget>();
        w->SetTag(SV(tag));
        return w;
    }
}

// === value parsers ===

TEST_CASE("css-values: ParseLength")
{
    CHECK(ParseLength(SV(u8"4")).Value() == doctest::Approx(4.0f));
    CHECK(ParseLength(SV(u8"2.5")).Value() == doctest::Approx(2.5f));
    CHECK(ParseLength(SV(u8"10px")).Value() == doctest::Approx(10.0f)); // unit ignored
    CHECK(ParseLength(SV(u8"-3")).Value() == doctest::Approx(-3.0f));
    CHECK_FALSE(ParseLength(SV(u8"abc")).HasValue());
    CHECK_FALSE(ParseLength(SV(u8"")).HasValue());
}

TEST_CASE("css-values: ParseColor hex / named / rgb")
{
    CHECK(ParseColor(SV(u8"#ff0000")).Value().r == doctest::Approx(1.0f));
    CHECK(ParseColor(SV(u8"#00ff00")).Value().g == doctest::Approx(1.0f));
    CHECK(ParseColor(SV(u8"#f00")).Value().r == doctest::Approx(1.0f)); // shorthand
    CHECK(ParseColor(SV(u8"#0000ff80")).Value().a == doctest::Approx(128.0f / 255.0f)); // alpha
    CHECK(ParseColor(SV(u8"white")).Value().r == doctest::Approx(1.0f));
    CHECK(ParseColor(SV(u8"blue")).Value().b == doctest::Approx(1.0f));
    CHECK(ParseColor(SV(u8"rgb(0,0,255)")).Value().b == doctest::Approx(1.0f));
    CHECK(ParseColor(SV(u8"rgba(255,0,0,0.5)")).Value().a == doctest::Approx(0.5f));
    CHECK_FALSE(ParseColor(SV(u8"#zz")).HasValue());
    CHECK_FALSE(ParseColor(SV(u8"notacolor")).HasValue());
}

TEST_CASE("css-values: ParseBool")
{
    CHECK(ParseBool(SV(u8"true")).Value() == true);
    CHECK(ParseBool(SV(u8"false")).Value() == false);
    CHECK(ParseBool(SV(u8"1")).Value() == true);
    CHECK(ParseBool(SV(u8"0")).Value() == false);
    CHECK_FALSE(ParseBool(SV(u8"maybe")).HasValue());
}

TEST_CASE("css-values: ParseThickness shorthand (top/right/bottom/left)")
{
    Thickness one = ParseThickness(SV(u8"4")).Value();
    CHECK(one.Left == 4.0f); CHECK(one.Top == 4.0f); CHECK(one.Right == 4.0f); CHECK(one.Bottom == 4.0f);

    Thickness two = ParseThickness(SV(u8"2 6")).Value(); // vertical=2, horizontal=6
    CHECK(two.Top == 2.0f); CHECK(two.Bottom == 2.0f);
    CHECK(two.Left == 6.0f); CHECK(two.Right == 6.0f);

    Thickness four = ParseThickness(SV(u8"1 2 3 4")).Value(); // top right bottom left
    CHECK(four.Top == 1.0f); CHECK(four.Right == 2.0f); CHECK(four.Bottom == 3.0f); CHECK(four.Left == 4.0f);
}

// === application ===

TEST_CASE("style-applier: background-color / padding / opacity")
{
    StyleSheet sheet = CSSParser::Parse(SV(u8"button { background-color: #0000ff; padding: 4; opacity: 0.5; }"));
    auto w = Widget(u8"button");
    ApplyStyle(*w.Get(), sheet.Resolve(*w.Get()));

    REQUIRE(w->GetBackground() != nullptr);
    auto* bg = core::Cast<RectangleDrawable>(w->GetBackground());
    REQUIRE(bg != nullptr);
    CHECK(bg->GetColor().b == doctest::Approx(1.0f)); // blue
    CHECK(w->GetPadding().Left == doctest::Approx(4.0f));
    CHECK(w->GetAlpha() == doctest::Approx(0.5f));
}

TEST_CASE("style-applier: width/height, enabled, visibility, margin")
{
    StyleSheet sheet = CSSParser::Parse(
        SV(u8"button { width: 120; height: 40; enabled: false; visibility: hidden; margin: 1 2 3 4; }"));
    auto w = Widget(u8"button");
    ApplyStyle(*w.Get(), sheet.Resolve(*w.Get(), MediaContext{}, /*applyPseudo*/ false));

    CHECK(w->GetSize().x == doctest::Approx(120.0f));
    CHECK(w->GetSize().y == doctest::Approx(40.0f));
    CHECK_FALSE(w->IsEnabled());
    CHECK_FALSE(w->IsVisible());
    CHECK(w->GetMargin().Top == doctest::Approx(1.0f));
    CHECK(w->GetMargin().Left == doctest::Approx(4.0f));
}

TEST_CASE("style-applier: cascade drives the applied value")
{
    StyleSheet sheet = CSSParser::Parse(
        SV(u8"button { background-color: black; } .primary { background-color: red; }"));
    auto w = Widget(u8"button");
    w->AddClass(SV(u8"primary"));
    ApplyStyle(*w.Get(), sheet.Resolve(*w.Get()));

    auto* bg = core::Cast<RectangleDrawable>(w->GetBackground());
    REQUIRE(bg != nullptr);
    CHECK(bg->GetColor().r == doctest::Approx(1.0f)); // .primary (red) beats button (black)
    CHECK(bg->GetColor().g == doctest::Approx(0.0f));
}
