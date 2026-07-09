// Draconic GUI - :theme partition
//
// A theme is just a default stylesheet: built-in CSS strings that style the standard widget
// tags (label/button/textfield/listbox/combobox/window/menu/...) so an app gets a consistent
// look without hand-authoring styles. Parse one with CSSParser and set it on a StyleManager
// (optionally with an IResourceProvider for background-image/font-family). Switching themes is
// just swapping the sheet - the StyleManager already hot-reloads. These cover the CSS-driven
// surface (backgrounds, text color, padding, button state); widgets' own accent draws (slider
// fill, checkbox mark) remain their code defaults until skin resources are wired.

module;
#include "Core/Prelude.h"

export module draconic.gui:theme;

import draconic.core;   // StringView

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    // Built-in dark theme.
    [[nodiscard]] inline core::StringView DefaultDarkThemeCSS()
    {
        return core::StringView(u8R"(
            label     { color: #d2d8e0; }
            button    { background-color: #33507a; color: #f4f7fb; padding: 9; transition: opacity 0.15s; }
            button:hover  { opacity: 0.9; }
            button:active { opacity: 0.78; }
            textfield { background-color: #20242b; color: #f0f4f8; padding: 6; }
            listbox   { background-color: #14161a; }
            combobox  { background-color: #20242b; color: #eef2f7; padding: 4; }
            scrollview { background-color: #14161a; }
            window    { background-color: #1a1d24; }
            menu      { background-color: #1a1d24; }
            tabwidget { background-color: #1a1d24; }
        )");
    }

    // Built-in light theme (same structure, light palette).
    [[nodiscard]] inline core::StringView DefaultLightThemeCSS()
    {
        return core::StringView(u8R"(
            label     { color: #1c2530; }
            button    { background-color: #cdd8ea; color: #17202b; padding: 9; transition: opacity 0.15s; }
            button:hover  { opacity: 0.9; }
            button:active { opacity: 0.78; }
            textfield { background-color: #ffffff; color: #10161d; padding: 6; }
            listbox   { background-color: #f2f4f8; }
            combobox  { background-color: #ffffff; color: #10161d; padding: 4; }
            scrollview { background-color: #f2f4f8; }
            window    { background-color: #e6eaf1; }
            menu      { background-color: #f2f4f8; }
            tabwidget { background-color: #e6eaf1; }
        )");
    }
}
