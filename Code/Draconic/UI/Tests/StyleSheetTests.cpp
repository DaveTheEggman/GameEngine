// Ported from Sedulous.UI.Tests/src/StyleSheetTests.bf - the PURE-DATA subset (StyleValue accessors,
// StyleRule fluent + string lifecycle, StyleSelector specificity, ForAll empty-selector). The
// integration cases (Resolve_*/Specificity_* cascade/Inheritance_*/RefCounted_*/Palette_*) drive
// View.ResolveStyle + the ViewGroup/RootView/UIContext tree and are DEFERRED until that cluster lands.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import draconic.core;
import draconic.ui;

using namespace draconic::ui;
using namespace draconic::core;
namespace core = draconic::core;

namespace
{
    // Minimal concrete View for type/selector tests (grows into the ported TestHelpers later).
    class TestView : public View
    {
        DRACONIC_OBJECT(TestView, View)
    };
    DRACONIC_DEFINE_OBJECT(TestView, "draconic::ui::tests")
}

// === StyleValue accessors ===

TEST_CASE("stylesheet: StyleValue_ColorAccessor")
{
    StyleValue val = StyleValue::ColorVal(core::Color::Red);
    CHECK(val.AsColor().HasValue());
    CHECK_FALSE(val.AsFloat().HasValue());
    CHECK(val.AsDrawable() == nullptr);
}

TEST_CASE("stylesheet: StyleValue_FloatAccessor")
{
    StyleValue val = StyleValue::FloatVal(42.0f);
    CHECK(val.AsFloat().HasValue());
    CHECK_FALSE(val.AsColor().HasValue());
}

TEST_CASE("stylesheet: StyleValue_None")
{
    StyleValue val = StyleValue::None();
    CHECK_FALSE(val.AsColor().HasValue());
    CHECK_FALSE(val.AsFloat().HasValue());
    CHECK_FALSE(val.AsThickness().HasValue());
    CHECK(val.AsDrawable() == nullptr);
    CHECK_FALSE(val.AsBool().HasValue());
}

// === StyleRule ===

TEST_CASE("stylesheet: StyleRule_FluentSet")
{
    StyleRule rule;
    rule.Set(StyleProperty::TextColor, core::Color::Red)
        .Set(StyleProperty::FontSize, 16.0f)
        .Set(StyleProperty::Padding, Thickness{ 4.0f });

    CHECK(rule.PropertyCount() == 3u);
    CHECK(rule.GetValue(StyleProperty::TextColor).HasValue());
    CHECK(rule.GetValue(StyleProperty::FontSize).HasValue());
    CHECK(rule.GetValue(StyleProperty::Padding).HasValue());
    CHECK_FALSE(rule.GetValue(StyleProperty::Background).HasValue());
}

TEST_CASE("stylesheet: StringValue_RoundTrip")
{
    StyleRule rule;
    rule.Set(StyleProperty::FontFamily, StringView{ u8"Roboto" });

    Optional<StyleValue> v = rule.GetValue(StyleProperty::FontFamily);
    REQUIRE(v.HasValue());
    CHECK(v.Value().AsString().HasValue());
    CHECK(v.Value().AsString().Value() == StringView{ u8"Roboto" });
}

TEST_CASE("stylesheet: StringValue_OverwriteFreesPrevious")
{
    // RAII: overwriting an entry drops the previous StyleValue (its owned String). No leak.
    StyleRule rule;
    rule.Set(StyleProperty::FontFamily, StringView{ u8"Roboto" });
    rule.Set(StyleProperty::FontFamily, StringView{ u8"JungleAdventurer" });
    rule.Set(StyleProperty::FontFamily, StringView{ u8"AttackOfMonster" });

    CHECK(rule.GetValue(StyleProperty::FontFamily).Value().AsString().Value() == StringView{ u8"AttackOfMonster" });
    CHECK(rule.PropertyCount() == 1u);
}

TEST_CASE("stylesheet: StringValue_RemoveFreesString")
{
    StyleRule rule;
    rule.Set(StyleProperty::FontFamily, StringView{ u8"Roboto" });
    CHECK(rule.Remove(StyleProperty::FontFamily));
    CHECK_FALSE(rule.GetValue(StyleProperty::FontFamily).HasValue());
}

TEST_CASE("stylesheet: StringValue_DestructorFrees")
{
    // Construct/destroy many string-holding rules; RAII frees each (no leak).
    for (int i = 0; i < 16; ++i)
    {
        StyleRule rule;
        rule.Set(StyleProperty::FontFamily, StringView{ u8"Roboto" });
        rule.Set(StyleProperty::FontFamily, StringView{ u8"JungleAdventurer" });
        CHECK(rule.GetValue(StyleProperty::FontFamily).Value().AsString().Value() == StringView{ u8"JungleAdventurer" });
    }
}

// === StyleSelector ===

TEST_CASE("stylesheet: Selector_NullMatchesAnything")
{
    StyleSelector sel;
    TestView view;
    CHECK(sel.Matches(view, ControlState::Normal));
    CHECK(sel.Matches(view, ControlState::Hover));
    CHECK(sel.Specificity() == 0);
}

TEST_CASE("stylesheet: Selector_Specificity_Computed")
{
    StyleSelector selType;
    selType.ViewType = &TestView::StaticType();
    CHECK(selType.Specificity() == 1);

    StyleSelector selClass;
    selClass.AddClass(StringView{ u8"primary" });
    CHECK(selClass.Specificity() == 10);

    StyleSelector selState;
    selState.State = ControlState::Hover;
    CHECK(selState.Specificity() == 1);

    StyleSelector selAll;
    selAll.ViewType = &TestView::StaticType();
    selAll.AddClass(StringView{ u8"primary" });
    selAll.State = ControlState::Hover;
    CHECK(selAll.Specificity() == 12);
}

// === ForAll rule (empty selector) ===

TEST_CASE("stylesheet: ForAll_HasEmptySelector_SpecificityZero")
{
    core::RefPtr<StyleSheet> sheet = core::MakeRef<StyleSheet>(core::DefaultAllocator());
    StyleRule& rule = sheet->ForAll();
    CHECK(rule.Selector.IsEmpty());
    CHECK(rule.Selector.Specificity() == 0);
    CHECK(sheet->RuleCount() == 1u);
}
