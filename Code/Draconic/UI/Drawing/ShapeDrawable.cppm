// Draconic UI - :shape_drawable partition
//
// Delegate-based custom drawing without subclassing. Ported from
// Sedulous.UI/src/Drawing/ShapeDrawable.bf (Beef delegate -> core::Function).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui:shape_drawable;

import draconic.core; // Function, Rectangle
import :drawable;
import :draw_context;

using namespace draconic::core;

export namespace draconic::ui
{
    class ShapeDrawable : public Drawable
    {
        DRACONIC_OBJECT(ShapeDrawable, Drawable)
    public:
        using DrawFn = Function<void(UIDrawContext&, const Rectangle&)>;

        explicit ShapeDrawable(DrawFn drawFn) : m_drawFn(Move(drawFn)) {}

        void Draw(UIDrawContext& ctx, const Rectangle& bounds) override
        {
            if (m_drawFn)
            {
                m_drawFn(ctx, bounds);
            }
        }

    private:
        DrawFn m_drawFn;
    };

    DRACONIC_DEFINE_OBJECT(ShapeDrawable, "draconic::ui")
}
