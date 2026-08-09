// Draconic UI - :color_drawable partition
//
// Fills bounds with a solid color. Ported from Sedulous.UI/src/Drawing/ColorDrawable.bf.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui:color_drawable;

import foundation.core; // Color, Rectangle
import :drawable;
import :draw_context;

using namespace foundation::core;
namespace core = foundation::core; // to name core::Color where the field shadows the type

export namespace foundation::ui
{
    class ColorDrawable : public Drawable
    {
        RTTI_OBJECT(ColorDrawable, Drawable)
    public:
        core::Color Color{};

        ColorDrawable() = default;
        explicit ColorDrawable(core::Color color) : Color(color) {}

        void Draw(UIDrawContext& ctx, const Rectangle& bounds) override
        {
            if (Color.a > 0.0f)
            {
                ctx.VG().FillRect(bounds, Color);
            }
        }
    };

    RTTI_DEFINE_OBJECT(ColorDrawable, "rtti::ui")
}
