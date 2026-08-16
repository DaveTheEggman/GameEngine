// The toolkit-fragment parity gates (ui-theme-migration.md P3): each embedded toolkit .sss
// fragment, parsed and merged by ToolkitThemeExtension::Apply, must declare the SAME styling
// as the legacy C++ builder (ApplyLegacyForParity) - rule for rule, value for value - BEFORE
// the C++ body may be deleted. Comparator shared from UI.Tests/ThemeParityHelpers.h. Retires
// when the consistency pass deliberately supersedes the legacy look.
#include <doctest/doctest.h>
#include <cstdio>
#include "Core/Prelude.h"
import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
#include "ThemeParityHelpers.h"

using namespace foundation::ui;
using namespace foundation::ui::toolkit;
using namespace foundation::core;

namespace
{
    void CheckToolkitParity(StringView fragment, ThemePalette palette)
    {
        // Guard against a vacuous pass: if the embedded fragment failed to parse, Apply falls
        // back to the legacy builder and both sheets would trivially match. Prove the fragment
        // itself parses to a non-empty sheet first.
        {
            StyleSheetLoader loader;
            loader.SetPalette(palette);
            RefPtr<StyleSheet> parsed = loader.Load(fragment);
            REQUIRE(parsed.Get() != nullptr);
            REQUIRE(parsed->RuleCount() > 0);
        }

        ToolkitThemeExtension ext;
        StyleSheet fromSss;
        ext.Apply(fromSss, palette); // the REAL path: parse + MergeFrom
        StyleSheet fromLegacy;
        ToolkitThemeExtension::ApplyLegacyForParity(fromLegacy, palette);
        theme_parity::CheckSheetParity(fromSss, fromLegacy);
    }
}

TEST_CASE("toolkit-parity: toolkit-dark.sss matches the legacy isDark rules")
{
    CheckToolkitParity(EmbeddedToolkitThemes::Dark(), ThemePalette::Dark());
}

TEST_CASE("toolkit-parity: toolkit-light.sss matches the legacy light rules")
{
    CheckToolkitParity(EmbeddedToolkitThemes::Light(), ThemePalette::Light());
}

TEST_CASE("toolkit-parity: the editor's live palette (GraphiteOrange, dark branch)")
{
    // A warm dark palette exercises every palette-derived expression with non-default inputs.
    CheckToolkitParity(EmbeddedToolkitThemes::Dark(), ThemePalette::GraphiteOrange());
}
