// Draconic UI - :separator partition
//
// Horizontal or vertical divider line. Ported from Sedulous.UI/src/Controls/Separator.bf.
// (The `Orientation` property shadows the enum type, so enum values are fully qualified.)

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui:separator;

import foundation.core;
import foundation.vg;
import :view;
import :property;
import :box_constraints;
import :style_property;
import :draw_context;
import :enums; // Orientation

using namespace foundation::core;

export namespace foundation::ui
{
    class Separator : public View
    {
        DRACONIC_OBJECT(Separator, View)
    public:
        Property<::foundation::ui::Orientation> Orientation{::foundation::ui::Orientation::Horizontal};
        Property<f32> SeparatorThickness{1.0f};

        Separator()
        {
            Orientation.SetOwner(this);
            SeparatorThickness.SetOwner(this);
        }
        explicit Separator(::foundation::ui::Orientation orientation) : Separator()
        {
            Orientation.SetSilent(orientation);
        }

    protected:
        void OnMeasure(BoxConstraints constraints) override
        {
            if (Orientation.Value() == ::foundation::ui::Orientation::Horizontal)
                MeasuredSize = Float2{constraints.ConstrainWidth(constraints.MaxWidth),
                                      constraints.ConstrainHeight(SeparatorThickness.Value())};
            else
                MeasuredSize = Float2{constraints.ConstrainWidth(SeparatorThickness.Value()),
                                      constraints.ConstrainHeight(constraints.MaxHeight)};
        }
        void OnDraw(UIDrawContext& ctx) override
        {
            const Color color =
                ResolveStyleColor(StyleProperty::BorderColor,
                                  Color{80.0f / 255.0f, 80.0f / 255.0f, 90.0f / 255.0f, 1.0f});
            ctx.VG().FillRect(Rectangle{0, 0, Width(), Height()}, color);
        }
    };

    DRACONIC_DEFINE_OBJECT(Separator, "rtti::ui")
}
