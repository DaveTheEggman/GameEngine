// Draconic UI - :rounded_rect_drawable partition
//
// Filled rounded rectangle with optional border; per-corner radii via vg::CornerRadii.
// Ported from Sedulous.UI/src/Drawing/RoundedRectDrawable.bf.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui:rounded_rect_drawable;

import foundation.core; // Color, Rectangle
import foundation.vg;   // CornerRadii
import :drawable;
import :draw_context;

using namespace foundation::core;
namespace core = foundation::core;
namespace vg = foundation::vg;

export namespace foundation::ui
{
    class RoundedRectDrawable : public Drawable
    {
        DRACONIC_OBJECT(RoundedRectDrawable, Drawable)
    public:
        core::Color FillColor{};
        core::Color BorderColor = core::Color::Transparent;
        f32 BorderWidth = 0.0f;
        vg::CornerRadii Radii{};

        RoundedRectDrawable() = default;
        /// Uniform corner radius.
        explicit RoundedRectDrawable(core::Color fill, f32 cornerRadius = 0.0f,
                                     core::Color borderColor = core::Color::Transparent,
                                     f32 borderWidth = 0.0f)
            : FillColor(fill), BorderColor(borderColor), BorderWidth(borderWidth),
              Radii(cornerRadius)
        {
        }
        /// Per-corner radii.
        RoundedRectDrawable(core::Color fill, vg::CornerRadii radii,
                            core::Color borderColor = core::Color::Transparent,
                            f32 borderWidth = 0.0f)
            : FillColor(fill), BorderColor(borderColor), BorderWidth(borderWidth), Radii(radii)
        {
        }

        void Draw(UIDrawContext& ctx, const Rectangle& bounds) override
        {
            if (!Radii.IsZero())
            {
                if (FillColor.a > 0.0f)
                {
                    ctx.VG().FillRoundedRect(bounds, Radii, FillColor);
                }
                if (BorderColor.a > 0.0f && BorderWidth > 0.0f)
                {
                    ctx.VG().StrokeRoundedRect(bounds, Radii, BorderColor, BorderWidth);
                }
            }
            else
            {
                if (FillColor.a > 0.0f)
                {
                    ctx.VG().FillRect(bounds, FillColor);
                }
                if (BorderColor.a > 0.0f && BorderWidth > 0.0f)
                {
                    ctx.VG().StrokeRect(bounds, BorderColor, BorderWidth);
                }
            }
        }
    };

    DRACONIC_DEFINE_OBJECT(RoundedRectDrawable, "rtti::ui")
}
