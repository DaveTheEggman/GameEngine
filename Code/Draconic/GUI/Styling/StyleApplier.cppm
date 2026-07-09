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
import :thickness;
import :node;
import :ui_node;
import :ui_widget;
import :drawable;
import :rectangle_drawable;
import :style_sheet;    // ResolvedStyle
import :css_values;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    // Apply the supported declarations of `style` to `node` (and, for margin, if it is a UIWidget).
    inline void ApplyStyle(UINode& node, const ResolvedStyle& style)
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
    }
}
