// Built-in theme sheet gates (ui-theme-migration.md consistency pass). The parity tests
// retired when the sheets were RE-AUTHORED to the shared design system (the legacy C++
// builders are superseded, kept only as the parse-failure belt until visual sign-off).
// These gates check what must stay true regardless of look: every sheet parses non-empty
// under its palettes, the design-system ramps hold (16/14/12), and the icon vocabulary
// resolves to real drawables (svg() must never silently null - the P0 finding).
#include <doctest/doctest.h>
#include <cstdio>
#include "Core/Prelude.h"
import foundation.core;
import foundation.ui;
#include "TestHelpers.h"

using namespace foundation::ui;
using namespace foundation::core;
namespace core = foundation::core;

namespace
{
    RefPtr<StyleSheet> LoadTheme(StringView sss, ThemePalette palette)
    {
        StyleSheetLoader loader;
        loader.SetPalette(palette);
        return loader.Load(sss);
    }

    /// Merged lookup: last rule that targets exactly (type, no class, no state, pseudo) wins.
    const StyleValue* FindValue(const StyleSheet& sheet, const TypeInfo* type, StringView pseudo,
                                StyleProperty prop)
    {
        const StyleValue* found = nullptr;
        for (usize r = 0; r < sheet.RuleCount(); ++r)
        {
            const StyleRule& rule = sheet.GetRule(r);
            const StyleSelector& sel = rule.Selector;
            if (sel.ViewType != type || sel.StyleClasses.Size() > 0 || sel.State.HasValue())
            {
                continue;
            }
            const StringView rulePseudo =
                sel.PseudoElement.HasValue() ? sel.PseudoElement.Value().AsView() : StringView{};
            if (rulePseudo != pseudo)
            {
                continue;
            }
            for (usize i = 0; i < rule.PropertyCount(); ++i)
            {
                if (rule.GetProperty(i).Prop == prop)
                {
                    found = &rule.GetProperty(i).Value;
                }
            }
        }
        return found;
    }

    void CheckFontSize(const StyleSheet& sheet, const TypeInfo* type, f32 expected)
    {
        const StyleValue* v = FindValue(sheet, type, {}, StyleProperty::FontSize);
        REQUIRE(v != nullptr);
        REQUIRE(v->AsFloat().HasValue());
        CHECK(v->AsFloat().Value() == doctest::Approx(expected));
    }

    usize CountSvgIconRules(const StyleSheet& sheet)
    {
        usize count = 0;
        for (usize r = 0; r < sheet.RuleCount(); ++r)
        {
            const StyleRule& rule = sheet.GetRule(r);
            for (usize i = 0; i < rule.PropertyCount(); ++i)
            {
                if (Drawable* d = rule.GetProperty(i).Value.AsDrawable();
                    d != nullptr && core::Cast<SVGDrawable>(d) != nullptr)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    /// The full design-system contract shared by all built-in sheets.
    void CheckDesignSystem(const StyleSheet& sheet)
    {
        // Type ramp: 16 default / 14 inputs / 12 compact chrome.
        CheckFontSize(sheet, &View::StaticType(), 16.0f);
        CheckFontSize(sheet, &ButtonBase::StaticType(), 12.0f);
        CheckFontSize(sheet, &ComboBox::StaticType(), 12.0f);
        CheckFontSize(sheet, &EditText::StaticType(), 14.0f);
        CheckFontSize(sheet, &NumericField::StaticType(), 14.0f);
        CheckFontSize(sheet, &Expander::StaticType(), 14.0f);

        // Global defaults: themed text + accent on the View base type.
        CHECK(FindValue(sheet, &View::StaticType(), {}, StyleProperty::TextColor) != nullptr);
        CHECK(FindValue(sheet, &View::StaticType(), {}, StyleProperty::AccentColor) != nullptr);

        // Spacing scale: button pad 8 12 (CSS v h -> Thickness l12 t8 r12 b8), input pad 4 6.
        {
            const StyleValue* v =
                FindValue(sheet, &ButtonBase::StaticType(), {}, StyleProperty::Padding);
            REQUIRE(v != nullptr);
            REQUIRE(v->AsThickness().HasValue());
            const Thickness t = v->AsThickness().Value();
            CHECK(t.Left == 12.0f);
            CHECK(t.Top == 8.0f);
            CHECK(t.Right == 12.0f);
            CHECK(t.Bottom == 8.0f);
        }
        {
            const StyleValue* v =
                FindValue(sheet, &EditText::StaticType(), {}, StyleProperty::Padding);
            REQUIRE(v != nullptr);
            REQUIRE(v->AsThickness().HasValue());
            const Thickness t = v->AsThickness().Value();
            CHECK(t.Left == 6.0f);
            CHECK(t.Top == 4.0f);
        }

        // Icon vocabulary: 11 icon rules (check/radio marks, close, 4 chevrons, submenu
        // arrow, combo arrow, 2 spin arrows) - each must have resolved to a real SVGDrawable.
        CHECK(CountSvgIconRules(sheet) >= 11);

        // Sunken inputs carry a background drawable.
        CHECK(FindValue(sheet, &EditText::StaticType(), {}, StyleProperty::Background) !=
              nullptr);
    }
}

TEST_CASE("theme-sheets: dark.sss parses and holds the design system (Dark + GraphiteOrange)")
{
    RefPtr<StyleSheet> dark = LoadTheme(EmbeddedThemes::Dark(), ThemePalette::Dark());
    REQUIRE(dark.Get() != nullptr);
    REQUIRE(dark->RuleCount() > 0);
    CheckDesignSystem(*dark);

    RefPtr<StyleSheet> warm = LoadTheme(EmbeddedThemes::Dark(), ThemePalette::GraphiteOrange());
    REQUIRE(warm.Get() != nullptr);
    CheckDesignSystem(*warm);
}

TEST_CASE("theme-sheets: light.sss parses and holds the design system (tinted icons live)")
{
    RefPtr<StyleSheet> light = LoadTheme(EmbeddedThemes::Light(), ThemePalette::Light());
    REQUIRE(light.Get() != nullptr);
    REQUIRE(light->RuleCount() > 0);
    CheckDesignSystem(*light);
}

TEST_CASE("theme-sheets: rounded-dark.sss parses and holds the design system (editor palette)")
{
    RefPtr<StyleSheet> sheet =
        LoadTheme(EmbeddedThemes::RoundedDark(), ThemePalette::GraphiteOrange());
    REQUIRE(sheet.Get() != nullptr);
    REQUIRE(sheet->RuleCount() > 0);
    CheckDesignSystem(*sheet);

    // The rounded theme's identity on top of the shared logic: a global corner radius.
    const StyleValue* radius =
        FindValue(*sheet, &View::StaticType(), {}, StyleProperty::CornerRadius);
    REQUIRE(radius != nullptr);
    CHECK(radius->AsFloat().Value() == doctest::Approx(6.0f));
}

TEST_CASE("theme-sheets: Create() serves the parsed sheet (belt does not engage)")
{
    // DarkTheme::Create falls back to the legacy C++ builder ONLY on a parse failure of the
    // embedded sheet; a healthy build must serve the parsed one. The legacy builder has no
    // AccentColor on the View rule - its presence proves the parsed path.
    RefPtr<StyleSheet> sheet = DarkTheme::Create(ThemePalette::Dark());
    REQUIRE(sheet.Get() != nullptr);
    CHECK(FindValue(*sheet, &View::StaticType(), {}, StyleProperty::AccentColor) != nullptr);
}
