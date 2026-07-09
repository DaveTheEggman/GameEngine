// Draconic UI - :button partition
//
// Text button (the most common button type). Ported from Sedulous.UI/src/Controls/Button.bf. Text
// measuring/drawing is deferred (guarded by the null Fonts service): OnMeasure falls back to the font
// size for height, OnDraw draws the background chrome only. The Text/FontSize/FontFamily properties are
// kept for API completeness; text rendering wires them when IFontService lands.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui:button;

import draconic.core;
import :button_base;
import :view;
import :property;
import :style_property;
import :thickness;
import :box_constraints;
import :draw_context;
import :control_state;

using namespace draconic::core;

export namespace draconic::ui
{
    class Button : public ButtonBase
    {
        DRACONIC_OBJECT(Button, ButtonBase)
    public:
        Property<String> Text;
        Property<Optional<f32>> FontSize;
        Property<String> FontFamily;

        explicit Button(StringView text)
        {
            Text.SetOwner(this);
            FontSize.SetOwner(this);
            FontFamily.SetOwner(this, InvalidationKind::Visual);
            Text.SetSilent(String(text));
        }

        /// Set the button text.
        void SetText(StringView text) { Text.SetValue(String(text)); Invalidate(); }

    protected:
        void OnMeasure(BoxConstraints constraints) override
        {
            const Thickness pad = ResolveStyleThickness(StyleProperty::Padding, Thickness{ 12.0f, 8.0f });
            const BoxConstraints inner = constraints.Deflate(pad).Loosen();
            const f32 fontSize = FontSize.Value().HasValue() ? FontSize.Value().Value() : ResolveStyleFloat(StyleProperty::FontSize, 16.0f);

            // Text measuring deferred (Fonts service): width 0, height = font size.
            const f32 textW = 0.0f;
            const f32 textH = fontSize;
            MeasuredSize = Float2{ constraints.ConstrainWidth(Min(textW, inner.MaxWidth) + pad.TotalHorizontal()),
                                   constraints.ConstrainHeight(Min(textH, inner.MaxHeight) + pad.TotalVertical()) };
        }

        void OnDraw(UIDrawContext& ctx) override
        {
            const Rectangle bounds{ 0, 0, Width(), Height() };
            DrawButtonBackground(ctx, bounds, GetControlState());
            // Text drawing deferred until the Fonts service is wired.
        }
    };

    DRACONIC_DEFINE_OBJECT(Button, "draconic::ui")
}
