// UI - :rounded_rect_drawable partition
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
import :thickness; // DrawablePadding
import :draw_context;

using namespace foundation::core;
namespace core = foundation::core;
namespace vg = foundation::vg;

export namespace foundation::ui
{
    class RoundedRectDrawable : public Drawable
    {
        RTTI_OBJECT(RoundedRectDrawable, Drawable)
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
            // Border strokes are INSET (stroke center pulled in by half the width) so the whole
            // border lies INSIDE bounds - border-box painting (ui-box-model.md). VG strokes are
            // centered on the path, so an un-inset stroke used to hang half outside the view,
            // where ClipsContent amputated it and adjacent siblings' borders overlapped.
            const f32 inset = BorderWidth * 0.5f;
            const Rectangle borderRect{bounds.x + inset, bounds.y + inset,
                                       Max(0.0f, bounds.width - BorderWidth),
                                       Max(0.0f, bounds.height - BorderWidth)};
            if (!Radii.IsZero())
            {
                if (FillColor.a > 0.0f)
                {
                    ctx.VG().FillRoundedRect(bounds, Radii, FillColor);
                }
                if (BorderColor.a > 0.0f && BorderWidth > 0.0f)
                {
                    // Shrink radii with the inset so the stroke stays concentric.
                    vg::CornerRadii r = Radii;
                    r.topLeft = Max(0.0f, r.topLeft - inset);
                    r.topRight = Max(0.0f, r.topRight - inset);
                    r.bottomRight = Max(0.0f, r.bottomRight - inset);
                    r.bottomLeft = Max(0.0f, r.bottomLeft - inset);
                    ctx.VG().StrokeRoundedRect(borderRect, r, BorderColor, BorderWidth);
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
                    ctx.VG().StrokeRect(borderRect, BorderColor, BorderWidth);
                }
            }
        }

        /// Border-box: the border is chrome that content must clear (ui-box-model.md). Merged
        /// into padding by View::ResolveBoxMetrics / Panel::EffectivePadding.
        [[nodiscard]] Thickness DrawablePadding() const override
        {
            return Thickness{BorderWidth, BorderWidth, BorderWidth, BorderWidth};
        }
    };

    RTTI_DEFINE_OBJECT(RoundedRectDrawable, "rtti::ui")
}
