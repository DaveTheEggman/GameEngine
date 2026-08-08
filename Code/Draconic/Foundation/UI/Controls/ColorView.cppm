// Draconic UI - :color_view partition
//
// Solid color swatch view. Ported from Sedulous.UI/src/Controls/ColorView.bf.
// (The `Color` property shadows the Color type, so the type is fully qualified.)

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui:color_view;

import draconic.core;
import draconic.vg;
import :view;
import :property;
import :box_constraints;
import :draw_context;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::ui
{
    class ColorView : public View
    {
        DRACONIC_OBJECT(ColorView, View)
    public:
        Property<core::Color> Color{core::Color::White};
        Property<f32> PreferredWidth{0.0f};
        Property<f32> PreferredHeight{0.0f};

        ColorView()
        {
            Color.SetOwner(this, InvalidationKind::Visual);
            PreferredWidth.SetOwner(this);
            PreferredHeight.SetOwner(this);
        }
        explicit ColorView(core::Color color) : ColorView() { Color.SetSilent(color); }
        ColorView(core::Color color, f32 w, f32 h) : ColorView()
        {
            Color.SetSilent(color);
            PreferredWidth.SetSilent(w);
            PreferredHeight.SetSilent(h);
        }

    protected:
        void OnMeasure(BoxConstraints constraints) override
        {
            const f32 w = PreferredWidth.Value() > 0 ? PreferredWidth.Value() : 0.0f;
            const f32 h = PreferredHeight.Value() > 0 ? PreferredHeight.Value() : 0.0f;
            MeasuredSize = Float2{constraints.ConstrainWidth(w), constraints.ConstrainHeight(h)};
        }
        void OnDraw(UIDrawContext& ctx) override
        {
            ctx.VG().FillRect(Rectangle{0, 0, Width(), Height()}, Color.Value());
        }
    };

    DRACONIC_DEFINE_OBJECT(ColorView, "rtti::ui")
}
