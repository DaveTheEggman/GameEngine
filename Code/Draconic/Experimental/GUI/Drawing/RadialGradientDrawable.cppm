// Draconic GUI - :radial_gradient_drawable partition
//
// RadialGradientDrawable: a multi-stop radial gradient centered within the destination
// rect. Derived from eepp's RadialGradientDrawable (ColorStop list + center + radius);
// maps to VG's VGRadialGradientFill over a rect path. Center is a fraction of the bounds
// (0.5,0.5 = middle); RadiusScale multiplies the farthest-corner distance (CSS default).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.gui:radial_gradient_drawable;

import draconic.core; // Color, Float2, Array, Distance, Max
import draconic.vg;   // VGRadialGradientFill, GradientStop, PathBuilder
import :rect;
import :draw_context;
import :drawable;

using namespace foundation::core;
namespace core = foundation::core;
namespace vg = foundation::vg;

export namespace experimental::gui
{
    class RadialGradientDrawable : public Drawable
    {
        DRACONIC_OBJECT(RadialGradientDrawable, Drawable)
    public:
        core::Float2 Center{0.5f, 0.5f}; ///< Center as a fraction of the bounds.
        f32 RadiusScale = 1.0f;          ///< Radius = RadiusScale * farthest-corner distance.

        RadialGradientDrawable() = default;

        void AddStop(f32 offset, Color color) { m_stops.PushBack(vg::GradientStop{offset, color}); }
        void ClearStops() { m_stops.Clear(); }
        [[nodiscard]] usize StopCount() const noexcept { return m_stops.Size(); }

        void Draw(DrawContext& ctx, const Rect& dest) override
        {
            if (m_stops.Size() == 0)
                return;

            const core::Float2 centerPx{dest.x + Center.x * dest.width,
                                        dest.y + Center.y * dest.height};
            const f32 radius = RadiusScale * FarthestCornerDistance(dest, centerPx);

            vg::VGRadialGradientFill fill{centerPx, radius};
            for (const vg::GradientStop& stop : m_stops)
                fill.AddStop(stop.offset, stop.color);

            ctx.VG().FillPath(RectPath(dest), fill);
        }

    private:
        [[nodiscard]] static f32 FarthestCornerDistance(const Rect& r, core::Float2 c)
        {
            const f32 d0 = core::Distance(c, core::Float2{r.Left(), r.Top()});
            const f32 d1 = core::Distance(c, core::Float2{r.Right(), r.Top()});
            const f32 d2 = core::Distance(c, core::Float2{r.Left(), r.Bottom()});
            const f32 d3 = core::Distance(c, core::Float2{r.Right(), r.Bottom()});
            return core::Max(core::Max(d0, d1), core::Max(d2, d3));
        }

        [[nodiscard]] static vg::Path RectPath(const Rect& r)
        {
            vg::PathBuilder pb;
            pb.MoveTo(r.Left(), r.Top());
            pb.LineTo(r.Right(), r.Top());
            pb.LineTo(r.Right(), r.Bottom());
            pb.LineTo(r.Left(), r.Bottom());
            pb.Close();
            return pb.ToPath();
        }

        Array<vg::GradientStop> m_stops;
    };

    DRACONIC_DEFINE_OBJECT(RadialGradientDrawable, "rtti::gui")
}
