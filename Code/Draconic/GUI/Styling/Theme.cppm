// Draconic GUI - :theme partition
//
// A theme is just a default stylesheet: built-in CSS that styles the standard widget tags plus
// their pseudo-element parts (slider::fill, window::title, scrollbar::thumb, checkbox::mark,
// ...). Parse one with CSSParser and set it on a StyleManager (optionally with an
// IResourceProvider for background-image/font-family). Switching themes is just swapping the
// sheet - the StyleManager already hot-reloads.

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
            .panel    { background-color: #14161c; }
            label, menuitem, menusubmenu { color: #d2d8e0; }
            button    { background-color: #33507a; color: #f4f7fb; padding: 9; transition: opacity 0.15s; }
            button:hover  { opacity: 0.9; }
            button:active { opacity: 0.78; }
            .tab          { background-color: #23262e; color: #c2c9d4; }
            .tab.selected { background-color: #33507a; color: #f4f7fb; }
            textfield { background-color: #20242b; color: #f0f4f8; padding: 6; }
            listbox   { background-color: #14161a; }
            listbox::selection { background-color: #2f5f9e; }
            combobox  { background-color: #20242b; color: #eef2f7; padding: 4; }
            scrollview { background-color: #14161a; }
            scrollbar  { background-color: #1e2128; }
            scrollbar::thumb { background-color: #4a505c; }
            window     { background-color: #1a1d24; }
            window::title { background-color: #2b3444; }
            window::grip  { background-color: #3a4353; }
            menu       { background-color: #1a1d24; }
            menuitem::highlight, menusubmenu::highlight { background-color: #2f5f9e; }
            menuseparator::line { background-color: #3a3f4b; }
            tabwidget  { background-color: #1a1d24; }
            slider::track { background-color: #2b2f37; }
            slider::fill  { background-color: #4a90d9; }
            slider::thumb { background-color: #cfd6e0; }
            checkbox::box  { background-color: #8a93a2; }
            checkbox::mark { background-color: #4a90d9; }
            radio::ring { background-color: #8a93a2; }
            radio::dot  { background-color: #4a90d9; }
            progressbar::track { background-color: #2b2f37; }
            progressbar::fill  { background-color: #4a90d9; }
        )");
    }

    // Built-in light theme (same structure, light palette).
    [[nodiscard]] inline core::StringView DefaultLightThemeCSS()
    {
        return core::StringView(u8R"(
            .panel    { background-color: #dde3ec; }
            label, menuitem, menusubmenu { color: #1c2530; }
            button    { background-color: #cdd8ea; color: #17202b; padding: 9; transition: opacity 0.15s; }
            button:hover  { opacity: 0.9; }
            button:active { opacity: 0.78; }
            .tab          { background-color: #cfd6e2; color: #3a4453; }
            .tab.selected { background-color: #ffffff; color: #17202b; }
            textfield { background-color: #ffffff; color: #10161d; padding: 6; }
            listbox   { background-color: #f2f4f8; }
            listbox::selection { background-color: #bcd6f4; }
            combobox  { background-color: #ffffff; color: #10161d; padding: 4; }
            scrollview { background-color: #f2f4f8; }
            scrollbar  { background-color: #d5dbe4; }
            scrollbar::thumb { background-color: #a3abb8; }
            window     { background-color: #e6eaf1; }
            window::title { background-color: #c4cdda; }
            window::grip  { background-color: #b0b9c8; }
            menu       { background-color: #f2f4f8; }
            menuitem::highlight, menusubmenu::highlight { background-color: #9cc0ef; }
            menuseparator::line { background-color: #c2c8d2; }
            tabwidget  { background-color: #e6eaf1; }
            slider::track { background-color: #c2cad6; }
            slider::fill  { background-color: #2f6fb0; }
            slider::thumb { background-color: #3a4658; }
            checkbox::box  { background-color: #5a6474; }
            checkbox::mark { background-color: #2f6fb0; }
            radio::ring { background-color: #5a6474; }
            radio::dot  { background-color: #2f6fb0; }
            progressbar::track { background-color: #c2cad6; }
            progressbar::fill  { background-color: #2f6fb0; }
        )");
    }
}
