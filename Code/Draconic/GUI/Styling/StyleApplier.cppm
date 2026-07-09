// Draconic GUI - :style_applier partition
//
// ApplyStyle: writes a ResolvedStyle's known declarations onto a node's properties - the
// bridge from CSS strings to the widget setters built in earlier phases. This is the small,
// explicit stand-in for eepp's PropertySpecification/PropertyDefinition registry; it grows a
// case per supported property. background-image/font-family (which resolve to Drawable*/
// Font* via a resource provider) arrive with the resource-wiring increment.

module;
#include "Core/Prelude.h"

export module draconic.gui:style_applier;

import draconic.core;   // Cast, Optional, Color, Float2, MakeRef, DefaultAllocator
import draconic.fonts;  // CachedFont
import :thickness;
import :node;
import :ui_node;
import :ui_widget;
import :label;
import :drawable;
import :rectangle_drawable;
import :style_sheet;    // ResolvedStyle
import :css_values;
import :resource_provider;

using namespace draconic::core;
namespace core = draconic::core;
namespace fonts = draconic::fonts;

export namespace draconic::gui
{
    // Strip a CSS url(...) wrapper (and any quotes) to the inner resource name; returns the
    // value unchanged if it is not a url() form.
    [[nodiscard]] inline core::StringView ParseUrl(core::StringView value)
    {
        core::StringView v = core::Trim(value);
        if (v.Size() >= 5 && v.SubStr(0, 4) == core::StringView(u8"url(") && v[v.Size() - 1] == u8')')
            v = core::Trim(v.SubStr(4, v.Size() - 5));
        if (v.Size() >= 2 && (v[0] == u8'"' || v[0] == u8'\'') && v[v.Size() - 1] == v[0])
            v = v.SubStr(1, v.Size() - 2);
        return v;
    }

    // Apply the supported declarations of `style` to `node`. `resources` (optional) resolves
    // background-image/font-family; without it those properties are skipped.
    inline void ApplyStyle(UINode& node, const ResolvedStyle& style, IResourceProvider* resources = nullptr)
    {
        using core::StringView;

        if (style.Has(StringView(u8"background-color")))
            if (Optional<Color> c = ParseColor(style.Get(StringView(u8"background-color"))); c.HasValue())
                node.SetBackground(core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), c.Value()));

        if (style.Has(StringView(u8"padding")))
            if (Optional<Thickness> t = ParseThickness(style.Get(StringView(u8"padding"))); t.HasValue())
                node.SetPadding(t.Value());

        if (style.Has(StringView(u8"opacity")))
            if (Optional<f32> o = ParseLength(style.Get(StringView(u8"opacity"))); o.HasValue())
                node.SetAlpha(o.Value());

        // width / height (combined into one SetSize).
        {
            core::Float2 size = node.GetSize();
            bool changed = false;
            if (style.Has(StringView(u8"width")))
                if (Optional<f32> w = ParseLength(style.Get(StringView(u8"width"))); w.HasValue()) { size.x = w.Value(); changed = true; }
            if (style.Has(StringView(u8"height")))
                if (Optional<f32> h = ParseLength(style.Get(StringView(u8"height"))); h.HasValue()) { size.y = h.Value(); changed = true; }
            if (changed) node.SetSize(size);
        }

        if (style.Has(StringView(u8"enabled")))
            if (Optional<bool> e = ParseBool(style.Get(StringView(u8"enabled"))); e.HasValue())
                node.SetEnabled(e.Value());

        if (style.Has(StringView(u8"visibility")))
        {
            const StringView v = style.Get(StringView(u8"visibility"));
            if (v == StringView(u8"hidden")) node.SetVisible(false);
            else if (v == StringView(u8"visible")) node.SetVisible(true);
        }

        if (style.Has(StringView(u8"margin")))
            if (UIWidget* widget = core::Cast<UIWidget>(&node))
                if (Optional<Thickness> t = ParseThickness(style.Get(StringView(u8"margin"))); t.HasValue())
                    widget->SetMargin(t.Value());

        // Text color for text-bearing widgets (Label and its descendants: Button, MenuItem, ...).
        if (style.Has(StringView(u8"color")))
            if (Label* label = core::Cast<Label>(&node))
                if (Optional<Color> c = ParseColor(style.Get(StringView(u8"color"))); c.HasValue())
                    label->SetTextColor(c.Value());

        // Resource-backed props (need a provider).
        if (resources != nullptr)
        {
            if (style.Has(StringView(u8"background-image")))
            {
                const StringView name = ParseUrl(style.Get(StringView(u8"background-image")));
                if (name.Size() != 0)
                    if (Drawable* d = resources->GetDrawable(name))
                        node.SetBackground(RefPtr<Drawable>(d));
            }

            if (style.Has(StringView(u8"font-family")))
                if (Label* label = core::Cast<Label>(&node))
                {
                    const StringView family = ParseUrl(style.Get(StringView(u8"font-family"))); // strips quotes
                    const f32 size = ParseLength(style.Get(StringView(u8"font-size"), StringView(u8"16"))).ValueOr(16.0f);
                    if (family.Size() != 0 && size > 0.0f)
                        if (fonts::CachedFont* font = resources->GetFont(family, size))
                            label->SetFont(font);
                }
        }
    }
}
