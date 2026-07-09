// Draconic UI - :label partition
//
// Text display view. Ported from Sedulous.UI/src/Controls/Label.bf. Text measuring/drawing needs the
// Fonts service + VG DrawText (neither wired yet), so OnMeasure falls back to the font-size height and
// OnDraw is deferred. The display-only alignment/wrap/ellipsis properties are omitted until text
// rendering lands; Text/FontSize/FontFamily/TextColor are kept (used for measure + as ToggleButton content).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui:label;

import draconic.core;
import :view;
import :property;
import :box_constraints;
import :style_property;
import :draw_context;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::ui
{
    class Label : public View
    {
        DRACONIC_OBJECT(Label, View)
    public:
        Property<String> Text;
        Property<Optional<f32>> FontSize;
        Property<String> FontFamily;
        Property<Optional<core::Color>> TextColor;

        Label()
        {
            Text.SetOwner(this);
            FontSize.SetOwner(this);
            FontFamily.SetOwner(this, InvalidationKind::Visual);
            TextColor.SetOwner(this, InvalidationKind::Visual);
        }
        explicit Label(StringView text) : Label() { Text.SetSilent(String(text)); }

        /// Set text and return this for chaining.
        Label* SetText(StringView text) { Text.SetValue(String(text)); Invalidate(); return this; }

        [[nodiscard]] f32 GetBaseline() const override { return -1.0f; } // font metrics deferred

    protected:
        void OnMeasure(BoxConstraints constraints) override
        {
            const f32 fontSize = FontSize.Value().HasValue() ? FontSize.Value().Value() : ResolveStyleFloat(StyleProperty::FontSize, 16.0f);
            // Text measuring deferred: width 0, height = font size.
            MeasuredSize = Float2{ constraints.ConstrainWidth(0.0f), constraints.ConstrainHeight(fontSize) };
        }
        void OnDraw(UIDrawContext& ctx) override { (void)ctx; } // text drawing deferred
    };

    DRACONIC_DEFINE_OBJECT(Label, "draconic::ui")
}
