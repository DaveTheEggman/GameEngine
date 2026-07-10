// Ported from Sedulous.UI.Tests/src/SSSParserTests.bf.
//
// With the View cluster landed, the resolution-pipeline tests are now driven end-to-end through a real
// UIContext + RootView + TestView (registering "View"/element selectors explicitly, since
// UITypeRegistry::RegisterBuiltins is deferred until controls exist). Cases that need control classes
// (Button/CheckBox/ButtonBase: DrawableFactory_StateColors/StateRounded/Svg*, SubtypeMatching_*, Icon_*)
// remain DEFERRED until those controls are ported.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
#include "TestHelpers.h"

using namespace draconic::ui;
using namespace draconic::ui::tests;
using namespace draconic::core;
namespace core = draconic::core;

// Register the drawable factories + the element types these parser tests reference.
static void EnsureGlobals()
{
    StyleSheetLoader::InitializeGlobals();
    UITypeRegistry::Register(u8"View", &View::StaticType());
    UITypeRegistry::Register(u8"TestView", &TestView::StaticType());
    UITypeRegistry::Register(u8"TestGroup", &TestGroup::StaticType());
    UITypeRegistry::Register(u8"ButtonBase", &ButtonBase::StaticType());
    UITypeRegistry::Register(u8"Button", &Button::StaticType());
    UITypeRegistry::Register(u8"CheckBox", &CheckBox::StaticType());
}

// A UIContext + RootView with a stylesheet applied; the test adds views under `root`.
struct Fixture
{
    UIContext ctx;
    core::RefPtr<RootView> root = core::MakeRef<RootView>(core::DefaultAllocator());

    explicit Fixture(core::RefPtr<StyleSheet> sheet)
    {
        EnsureGlobals();
        Init(ctx, root.Get());
        ctx.SetStyleSheet(Move(sheet));
    }

    core::RefPtr<TestView> AddView()
    {
        core::RefPtr<TestView> v = core::MakeRef<TestView>(core::DefaultAllocator(), 50.0f, 30.0f);
        root->AddView(v.Get());
        return v;
    }
};

static core::RefPtr<StyleSheet> LoadSSS(StringView src) { StyleSheetLoader loader; return loader.Load(src); }

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

TEST_CASE("sss: Palette_Derivation")
{
    const Color dark = Palette::Darken(Color{ 200 / 255.0f, 200 / 255.0f, 200 / 255.0f, 1.0f }, 0.5f);
    CHECK(dark.r == doctest::Approx(100 / 255.0f));
    const Color light = Palette::Lighten(Color{ 0.0f, 0.0f, 0.0f, 1.0f }, 0.5f);
    CHECK(light.r > 100 / 255.0f);
}

// === Basic rule parsing (parse + inspect) ===

TEST_CASE("sss: CompoundStateRule")
{
    EnsureGlobals();
    core::RefPtr<StyleSheet> sheet = LoadSSS(u8R"(
        View { text-color: #ffffff; }
        View:checked:hover { text-color: #ff0000; }
    )");
    CHECK(sheet->RuleCount() == 2);
    const StyleRule& rule = sheet->GetRule(1);
    REQUIRE(rule.Selector.State.HasValue());
    const ControlState state = rule.Selector.State.Value();
    CHECK(HasFlag(state, ControlState::Checked));
    CHECK(HasFlag(state, ControlState::Hover));
}

TEST_CASE("sss: AllDrawableProperties_Parse")
{
    EnsureGlobals();
    core::RefPtr<StyleSheet> sheet = LoadSSS(u8R"(
        View { background: color(#111); checked-background: color(#222); menu-item-hover-drawable: color(#333); }
    )");
    CHECK(sheet->RuleCount() == 1);
    CHECK(sheet->GetRule(0).PropertyCount() == 3);
}

TEST_CASE("sss: AllColorProperties_Parse")
{
    EnsureGlobals();
    core::RefPtr<StyleSheet> sheet = LoadSSS(u8R"(
        View { text-color: #111; text-dim-color: #222; placeholder-color: #333; border-color: #444;
               cursor-color: #555; selection-color: #666; accent-color: #777; }
    )");
    CHECK(sheet->GetRule(0).PropertyCount() == 7);
}

TEST_CASE("sss: AllFloatProperties_Parse")
{
    EnsureGlobals();
    core::RefPtr<StyleSheet> sheet = LoadSSS(u8R"(
        View { font-size: 16; corner-radius: 4; border-width: 1; spacing: 8; opacity: 0.5; width: 100; height: 50; }
    )");
    CHECK(sheet->GetRule(0).PropertyCount() == 7);
}

TEST_CASE("sss: Comments_Ignored")
{
    EnsureGlobals();
    core::RefPtr<StyleSheet> sheet = LoadSSS(u8R"(
        /* comment */
        View { font-size: 14; /* inline */ }
    )");
    CHECK(sheet->RuleCount() == 1);
}

// === Resolution through the View cluster ===

TEST_CASE("sss: SimpleTypeRule")
{
    Fixture f(LoadSSS(u8"View { text-color: #ff0000; font-size: 16; }"));
    core::RefPtr<TestView> view = f.AddView();

    const Color color = view->ResolveStyleColor(StyleProperty::TextColor);
    CHECK(color.r == 1.0f);
    CHECK(color.g == 0);
    CHECK(color.b == 0);
    CHECK(view->ResolveStyleFloat(StyleProperty::FontSize) == doctest::Approx(16));
}

TEST_CASE("sss: ClassRule")
{
    Fixture f(LoadSSS(u8".primary { font-size: 24; }"));
    core::RefPtr<TestView> view = f.AddView();
    view->AddClass(u8"primary");
    CHECK(view->ResolveStyleFloat(StyleProperty::FontSize) == 24.0f);
}

TEST_CASE("sss: StateRule")
{
    Fixture f(LoadSSS(u8"View { font-size: 12; } View:disabled { font-size: 10; }"));
    core::RefPtr<TestView> view = f.AddView();
    view->IsEnabled = false;
    CHECK(view->ResolveStyleFloat(StyleProperty::FontSize) == doctest::Approx(10));
}

TEST_CASE("sss: PaletteVariable")
{
    Fixture f(LoadSSS(u8R"(
        @palette dark { text: #e0e0ee; }
        View { text-color: $text; }
    )"));
    core::RefPtr<TestView> view = f.AddView();
    CHECK(view->ResolveStyleColor(StyleProperty::TextColor).r == doctest::Approx(0xe0 / 255.0f));
}

TEST_CASE("sss: SetPalette_ThemePalette")
{
    StyleSheetLoader loader;
    loader.SetPalette(ThemePalette::Dark());
    Fixture f(loader.Load(u8"View { text-color: $text; }"));
    core::RefPtr<TestView> view = f.AddView();
    CHECK(view->ResolveStyleColor(StyleProperty::TextColor).r == doctest::Approx(220 / 255.0f));
}

TEST_CASE("sss: ColorFunction_Lighten")
{
    Fixture f(LoadSSS(u8"View { text-color: lighten(#000000, 50%); }"));
    core::RefPtr<TestView> view = f.AddView();
    CHECK(view->ResolveStyleColor(StyleProperty::TextColor).r > 100 / 255.0f);
}

TEST_CASE("sss: ColorFunction_Alpha")
{
    Fixture f(LoadSSS(u8"View { text-color: alpha(#ff0000, 0.5); }"));
    core::RefPtr<TestView> view = f.AddView();
    const Color c = view->ResolveStyleColor(StyleProperty::TextColor);
    CHECK(c.r == 1.0f);
    CHECK(c.a == doctest::Approx(0.5f));
}

TEST_CASE("sss: ColorFunction_Mix")
{
    Fixture f(LoadSSS(u8"View { text-color: mix(#000000, #ffffff, 0.5); }"));
    core::RefPtr<TestView> view = f.AddView();
    const f32 r = view->ResolveStyleColor(StyleProperty::TextColor).r;
    CHECK((r > 100 / 255.0f && r < 160 / 255.0f));
}

TEST_CASE("sss: NamedColor")
{
    Fixture f(LoadSSS(u8"View { text-color: white; }"));
    core::RefPtr<TestView> view = f.AddView();
    const Color c = view->ResolveStyleColor(StyleProperty::TextColor);
    CHECK((c.r == 1.0f && c.g == 1.0f && c.b == 1.0f));
}

TEST_CASE("sss: RgbFunction")
{
    Fixture f(LoadSSS(u8"View { text-color: rgb(100, 150, 200); }"));
    core::RefPtr<TestView> view = f.AddView();
    const Color c = view->ResolveStyleColor(StyleProperty::TextColor);
    CHECK(c.r == doctest::Approx(100 / 255.0f));
    CHECK(c.g == doctest::Approx(150 / 255.0f));
    CHECK(c.b == doctest::Approx(200 / 255.0f));
}

TEST_CASE("sss: RgbaFunction")
{
    Fixture f(LoadSSS(u8"View { text-color: rgba(100, 150, 200, 0.5); }"));
    core::RefPtr<TestView> view = f.AddView();
    const Color c = view->ResolveStyleColor(StyleProperty::TextColor);
    CHECK(c.r == doctest::Approx(100 / 255.0f));
    CHECK(c.a == doctest::Approx(0.5f));
}

// === Drawable factories (View-compatible) ===

TEST_CASE("sss: DrawableFactory_Color")
{
    Fixture f(LoadSSS(u8"View { background: color(#336699); }"));
    core::RefPtr<TestView> view = f.AddView();
    Drawable* bg = view->ResolveStyleDrawable(StyleProperty::Background);
    REQUIRE(bg != nullptr);
    CHECK(core::Cast<ColorDrawable>(bg) != nullptr);
}

TEST_CASE("sss: DrawableFactory_RoundedRect")
{
    Fixture f(LoadSSS(u8"View { background: rounded-rect(#336699, radius=6, border=#555555, border-width=1); }"));
    core::RefPtr<TestView> view = f.AddView();
    Drawable* bg = view->ResolveStyleDrawable(StyleProperty::Background);
    REQUIRE(bg != nullptr);
    RoundedRectDrawable* rrd = core::Cast<RoundedRectDrawable>(bg);
    REQUIRE(rrd != nullptr);
    CHECK(rrd->FillColor.r == doctest::Approx(0x33 / 255.0f));
    CHECK(rrd->BorderWidth == 1.0f);
}

TEST_CASE("sss: DrawableFactory_Gradient")
{
    Fixture f(LoadSSS(u8"View { background: gradient(left-to-right, #000000, #ffffff); }"));
    core::RefPtr<TestView> view = f.AddView();
    Drawable* bg = view->ResolveStyleDrawable(StyleProperty::Background);
    REQUIRE(bg != nullptr);
    GradientDrawable* gd = core::Cast<GradientDrawable>(bg);
    REQUIRE(gd != nullptr);
    CHECK(gd->Direction == GradientDirection::LeftToRight);
}

TEST_CASE("sss: DrawableFactory_StateList")
{
    Fixture f(LoadSSS(u8R"(
        View { background: state-list(normal=color(#111111), hover=color(#222222), pressed=color(#333333)); }
    )"));
    core::RefPtr<TestView> view = f.AddView();
    Drawable* bg = view->ResolveStyleDrawable(StyleProperty::Background);
    REQUIRE(bg != nullptr);
    CHECK(core::Cast<StateListDrawable>(bg) != nullptr);
}

TEST_CASE("sss: DrawableFactory_Layer")
{
    Fixture f(LoadSSS(u8"View { background: layer(color(#111111), color(#222222)); }"));
    core::RefPtr<TestView> view = f.AddView();
    Drawable* bg = view->ResolveStyleDrawable(StyleProperty::Background);
    REQUIRE(bg != nullptr);
    CHECK(core::Cast<LayerDrawable>(bg) != nullptr);
}

TEST_CASE("sss: DrawableFactory_Inset")
{
    Fixture f(LoadSSS(u8"View { background: inset(color(#336699), 4, 4, 4, 4); }"));
    core::RefPtr<TestView> view = f.AddView();
    Drawable* bg = view->ResolveStyleDrawable(StyleProperty::Background);
    REQUIRE(bg != nullptr);
    InsetDrawable* id = core::Cast<InsetDrawable>(bg);
    REQUIRE(id != nullptr);
    CHECK(id->Inset.Top == 4);
}

TEST_CASE("sss: ColorLiteral_AsDrawable")
{
    Fixture f(LoadSSS(u8"View { background: #336699; }"));
    core::RefPtr<TestView> view = f.AddView();
    Drawable* bg = view->ResolveStyleDrawable(StyleProperty::Background);
    REQUIRE(bg != nullptr);
    ColorDrawable* cd = core::Cast<ColorDrawable>(bg);
    REQUIRE(cd != nullptr);
    CHECK(cd->Color.r == doctest::Approx(0x33 / 255.0f));
}

TEST_CASE("sss: Variable_InDrawable")
{
    Fixture f(LoadSSS(u8R"(
        @palette dark { surface: #24242c; border: #3a3a45; }
        View { background: rounded-rect($surface, radius=6, border=$border, border-width=1); }
    )"));
    core::RefPtr<TestView> view = f.AddView();
    Drawable* bg = view->ResolveStyleDrawable(StyleProperty::Background);
    RoundedRectDrawable* rrd = core::Cast<RoundedRectDrawable>(bg);
    REQUIRE(rrd != nullptr);
    CHECK(rrd->FillColor.r == doctest::Approx(0x24 / 255.0f));
    CHECK(rrd->BorderColor.r == doctest::Approx(0x3a / 255.0f));
}

// === Property types ===

TEST_CASE("sss: ThicknessProperty_SingleValue")
{
    Fixture f(LoadSSS(u8"View { padding: 8; }"));
    core::RefPtr<TestView> view = f.AddView();
    const Thickness pad = view->ResolveStyleThickness(StyleProperty::Padding);
    CHECK((pad.Left == 8 && pad.Top == 8 && pad.Right == 8 && pad.Bottom == 8));
}

TEST_CASE("sss: ThicknessProperty_TwoValues")
{
    Fixture f(LoadSSS(u8"View { padding: 8 12; }"));
    core::RefPtr<TestView> view = f.AddView();
    const Thickness pad = view->ResolveStyleThickness(StyleProperty::Padding);
    CHECK(pad.Top == 8);
    CHECK(pad.Left == 12);
}

TEST_CASE("sss: ThicknessProperty_FourValues")
{
    Fixture f(LoadSSS(u8"View { padding: 1 2 3 4; }"));
    core::RefPtr<TestView> view = f.AddView();
    const Thickness pad = view->ResolveStyleThickness(StyleProperty::Padding);
    CHECK((pad.Top == 1 && pad.Right == 2 && pad.Bottom == 3 && pad.Left == 4));
}

TEST_CASE("sss: BoolProperty")
{
    Fixture f(LoadSSS(u8"View { word-wrap: true; }"));
    core::RefPtr<TestView> view = f.AddView();
    core::Optional<bool> b = view->ResolveStyle(StyleProperty::WordWrap).AsBool();
    REQUIRE(b.HasValue());
    CHECK(b.Value() == true);
}

TEST_CASE("sss: FloatProperty")
{
    Fixture f(LoadSSS(u8"View { corner-radius: 6; }"));
    core::RefPtr<TestView> view = f.AddView();
    CHECK(view->ResolveStyleFloat(StyleProperty::CornerRadius) == 6.0f);
}

TEST_CASE("sss: FontFamily_QuotedString")
{
    Fixture f(LoadSSS(u8"View { font-family: \"Attack Of Monster\"; }"));
    core::RefPtr<TestView> view = f.AddView();
    const StyleValue v = view->ResolveStyle(StyleProperty::FontFamily); // hold alive: AsString borrows its String
    core::Optional<StringView> s = v.AsString();
    REQUIRE(s.HasValue());
    CHECK(s.Value() == StringView(u8"Attack Of Monster"));
}

// === Cascade + inheritance ===

TEST_CASE("sss: Cascade_ClassBeatsType")
{
    Fixture f(LoadSSS(u8"View { font-size: 12; } .big { font-size: 24; }"));
    core::RefPtr<TestView> view = f.AddView();
    view->AddClass(u8"big");
    CHECK(view->ResolveStyleFloat(StyleProperty::FontSize) == 24.0f);
}

TEST_CASE("sss: Cascade_TypeStateBeatsType")
{
    Fixture f(LoadSSS(u8"View { text-color: #cccccc; } View:disabled { text-color: #333333; }"));
    core::RefPtr<TestView> view = f.AddView();
    view->IsEnabled = false;
    CHECK(view->ResolveStyleColor(StyleProperty::TextColor).r == doctest::Approx(0x33 / 255.0f));
}

TEST_CASE("sss: Inheritance_TextColor")
{
    Fixture f(LoadSSS(u8"View { text-color: #aabbcc; }"));
    core::RefPtr<TestGroup> group = core::MakeRef<TestGroup>(core::DefaultAllocator());
    f.root->AddView(group.Get());
    core::RefPtr<TestView> child = core::MakeRef<TestView>(core::DefaultAllocator(), 50.0f, 30.0f);
    group->AddView(child.Get());
    // Child inherits text-color from the View rule + inheritance walk.
    CHECK(child->ResolveStyleColor(StyleProperty::TextColor).r == doctest::Approx(0xaa / 255.0f));
}

TEST_CASE("sss: MultipleRules_SameType")
{
    Fixture f(LoadSSS(u8"View { font-size: 12; } View { text-color: #ff0000; }"));
    core::RefPtr<TestView> view = f.AddView();
    CHECK(view->ResolveStyleFloat(StyleProperty::FontSize) == 12.0f);
    CHECK(view->ResolveStyleColor(StyleProperty::TextColor).r == 1.0f);
}

// === Subtype matching + control-typed drawable factories (un-deferred: controls now exist) ===

TEST_CASE("sss: SubtypeMatching_ButtonMatchesButtonBase")
{
    Fixture f(LoadSSS(u8"ButtonBase { padding: 10 20; }"));
    auto btn = core::MakeRef<Button>(core::DefaultAllocator(), StringView(u8"Test"));
    f.root->AddView(btn.Get());
    const Thickness pad = btn->ResolveStyleThickness(StyleProperty::Padding);
    CHECK(pad.Top == 10);
    CHECK(pad.Left == 20);
}

TEST_CASE("sss: SubtypeMatching_ViewMatchesAll")
{
    Fixture f(LoadSSS(u8"View { font-size: 13; }"));
    auto btn = core::MakeRef<Button>(core::DefaultAllocator(), StringView(u8"B"));
    auto cb = core::MakeRef<CheckBox>(core::DefaultAllocator(), StringView(u8"C"));
    f.root->AddView(btn.Get());
    f.root->AddView(cb.Get());
    CHECK(btn->ResolveStyleFloat(StyleProperty::FontSize) == 13.0f);
    CHECK(cb->ResolveStyleFloat(StyleProperty::FontSize) == 13.0f);
}

TEST_CASE("sss: DrawableFactory_StateColors")
{
    Fixture f(LoadSSS(u8"ButtonBase { background: state-colors(#334455); }"));
    auto btn = core::MakeRef<Button>(core::DefaultAllocator(), StringView(u8"Test"));
    f.root->AddView(btn.Get());
    Drawable* bg = btn->ResolveStyleDrawable(StyleProperty::Background);
    REQUIRE(bg != nullptr);
    CHECK(core::Cast<StateListDrawable>(bg) != nullptr);
}

TEST_CASE("sss: DrawableFactory_StateRounded")
{
    Fixture f(LoadSSS(u8"ButtonBase { background: state-rounded(#334455, radius=4); }"));
    auto btn = core::MakeRef<Button>(core::DefaultAllocator(), StringView(u8"Test"));
    f.root->AddView(btn.Get());
    Drawable* bg = btn->ResolveStyleDrawable(StyleProperty::Background);
    REQUIRE(bg != nullptr);
    CHECK(core::Cast<StateListDrawable>(bg) != nullptr);
}

// Inline style="..." with a drawable function (regression: ApplyInlineStyle must register the drawable
// factory builtins - rounded-rect/gradient/state-* - itself, or the value falls back to a plain white
// color; StyleSheetLoader did this for .sss files but the inline path did not).
TEST_CASE("sss: InlineStyle_RoundedRectFunction")
{
    EnsureGlobals();
    auto view = core::MakeRef<TestView>(core::DefaultAllocator());
    SSSParser::ApplyInlineStyle(view.Get(),
        StringView(u8"background: rounded-rect(rgb(35, 38, 48), radius=12, border-width=2, border=rgb(80, 90, 110));"));

    Drawable* bg = view->ResolveStyleDrawable(StyleProperty::Background);
    REQUIRE(bg != nullptr);
    auto* rr = core::Cast<RoundedRectDrawable>(bg);
    REQUIRE(rr != nullptr); // not a fallback ColorDrawable
    CHECK(rr->FillColor.r == doctest::Approx(35 / 255.0f));
    CHECK(rr->FillColor.g == doctest::Approx(38 / 255.0f));
    CHECK(rr->FillColor.b == doctest::Approx(48 / 255.0f));
    CHECK(rr->Radii.topLeft == 12.0f);
    CHECK(rr->BorderWidth == 2.0f);
    CHECK(rr->BorderColor.r == doctest::Approx(80 / 255.0f));
}
