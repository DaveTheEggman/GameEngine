// Draconic UI - :state_list_drawable partition
//
// Maps ControlState flags -> Drawable with fallback lookup (tries the exact flag
// combination, then strips flags one at a time, ultimately falling back to Normal).
// Ported from Sedulous.UI/src/Drawing/StateListDrawable.bf.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui:state_list_drawable;

import draconic.core;   // HashMap, RefPtr, Rectangle
import :control_state;
import :drawable;
import :draw_context;

using namespace draconic::core;

export namespace draconic::ui
{
    class StateListDrawable : public Drawable
    {
        DRACONIC_OBJECT(StateListDrawable, Drawable)
    public:
        StateListDrawable() = default;

        /// Set the drawable for a state (or flag combination); replaces + releases any prior.
        void Set(ControlState state, RefPtr<Drawable> drawable)
        {
            m_drawables.InsertOrAssign(static_cast<u32>(state), drawable);
        }

        /// Get the drawable for a state: exact match, then strip flags high->low, then Normal.
        [[nodiscard]] Drawable* Get(ControlState state) const
        {
            const u32 key = static_cast<u32>(state);
            if (const RefPtr<Drawable>* exact = m_drawables.Find(key)) { return exact->Get(); }

            u32 remaining = key;
            static constexpr u32 kFlags[] = { 32u, 16u, 8u, 4u, 2u, 1u };
            for (u32 flag : kFlags)
            {
                if ((remaining & flag) == 0u) { continue; }
                remaining &= ~flag;
                if (const RefPtr<Drawable>* fb = m_drawables.Find(remaining)) { return fb->Get(); }
            }
            if (const RefPtr<Drawable>* normal = m_drawables.Find(0u)) { return normal->Get(); }
            return nullptr;
        }

        void Draw(UIDrawContext& ctx, const Rectangle& bounds) override
        {
            if (Drawable* d = Get(ControlState::Normal)) { d->Draw(ctx, bounds); }
        }
        void Draw(UIDrawContext& ctx, const Rectangle& bounds, ControlState state) override
        {
            if (Drawable* d = Get(state)) { d->Draw(ctx, bounds); }
        }

    private:
        HashMap<u32, RefPtr<Drawable>> m_drawables;
    };

    DRACONIC_DEFINE_OBJECT(StateListDrawable, "draconic::ui")
}
