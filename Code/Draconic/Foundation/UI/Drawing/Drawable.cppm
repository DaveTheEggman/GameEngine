// UI - :drawable partition
//
// Drawable: base class for composable visual primitives. Stateless; render into a bounds
// via UIDrawContext. Ported from Sedulous.UI/src/Drawing/Drawable.bf.
//
// Port note: Beef `RefCounted` -> our `Object` (which IS RefCounted) so concrete
// drawables get Cast<T> for the `as RoundedRectDrawable`-style downcasts (our RTTI
// everywhere; -fno-rtti). Drawables are not reflection-registered/scripted.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui:drawable;

import foundation.core; // Object, Optional, Float2, Rectangle
import :thickness;
import :control_state;
import :draw_context;

using namespace foundation::core;

export namespace foundation::ui
{
    class Drawable : public Object
    {
        RTTI_OBJECT(Drawable, Object)
    public:
        /// State-unaware draw.
        virtual void Draw(UIDrawContext& ctx, const Rectangle& bounds) = 0;

        /// State-aware draw - default delegates to the state-unaware overload.
        virtual void Draw(UIDrawContext& ctx, const Rectangle& bounds, ControlState state)
        {
            (void)state;
            Draw(ctx, bounds);
        }

        /// Optional natural size (e.g. icons/images). Empty = no intrinsic size.
        [[nodiscard]] virtual Optional<Float2> IntrinsicSize() const { return {}; }

        /// Padding contributed by this drawable (e.g. nine-slice borders). Layout can
        /// merge via max(drawablePadding, explicitPadding).
        [[nodiscard]] virtual Thickness DrawablePadding() const { return Thickness{}; }
    };

    RTTI_DEFINE_OBJECT(Drawable, "rtti::ui")
}
