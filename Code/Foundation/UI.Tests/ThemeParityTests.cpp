// The built-in-theme parity gates (ui-theme-migration.md P1/P2): each embedded stylesheet,
// parsed, must declare the SAME styling as its legacy C++ rule builder - rule for rule, value
// for value - BEFORE the C++ body may be deleted. Comparator lives in ThemeParityHelpers.h
// (shared with UI.Toolkit.Tests' toolkit-fragment gates). These tests retire when the
// consistency pass deliberately supersedes the legacy look.
#include <doctest/doctest.h>
#include <cstdio>
#include "Core/Prelude.h"
import foundation.core;
import foundation.ui;
#include "TestHelpers.h"
#include "ThemeParityHelpers.h"

using namespace foundation::ui;
using namespace foundation::core;

namespace
{
    void CheckThemeParity(StringView sss, RefPtr<StyleSheet> legacy, ThemePalette palette)
    {
        RefPtr<StyleSheet> parsed;
        {
            StyleSheetLoader loader;
            loader.SetPalette(palette);
            parsed = loader.Load(sss);
        }
        REQUIRE(parsed.Get() != nullptr);
        REQUIRE(legacy.Get() != nullptr);
        theme_parity::CheckSheetParity(*parsed, *legacy);
    }
}

TEST_CASE("theme-parity: dark.sss matches the legacy C++ DarkTheme rule-for-rule")
{
    CheckThemeParity(EmbeddedThemes::Dark(),
                     DarkTheme::CreateLegacyForParity(ThemePalette::Dark()), ThemePalette::Dark());
}

TEST_CASE("theme-parity: light.sss matches the legacy C++ LightTheme (tinted shared glyphs)")
{
    // Initialize the shared set so the tinted-variant path is exercised BY IDENTITY (the C++
    // theme and the sheet must acquire the SAME tinted instances).
    ThemeIconSet::Get().Initialize();
    CheckThemeParity(EmbeddedThemes::Light(),
                     LightTheme::CreateLegacyForParity(ThemePalette::Light()),
                     ThemePalette::Light());
    ThemeIconSet::Get().Shutdown();
}

TEST_CASE("theme-parity: rounded-dark.sss matches the legacy C++ RoundedDarkTheme (editor)")
{
    // The editor's live palette - per-corner spin radii + derived colors all covered.
    CheckThemeParity(EmbeddedThemes::RoundedDark(),
                     RoundedDarkTheme::CreateLegacyForParity(ThemePalette::GraphiteOrange()),
                     ThemePalette::GraphiteOrange());
}
