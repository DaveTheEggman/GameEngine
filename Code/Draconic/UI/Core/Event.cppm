// Draconic UI - :event partition
//
// Event<void(Args...)>: a minimal multicast delegate over core::Function, the port's
// equivalent of Beef's `Event<delegate void(...)>` used pervasively across Sedulous.UI
// (Property.Changed, Button.Clicked, ...). Add handlers; invoke via operator() / Invoke.

module;
#include "Core/Prelude.h"

export module draconic.ui:event;

import draconic.core;

using namespace draconic::core;

export namespace draconic::ui
{
    template <typename Signature>
    class Event; // primary template undefined; only void(Args...) is valid

    template <typename... Args>
    class Event<void(Args...)>
    {
    public:
        using Handler = Function<void(Args...)>;

        /// Register a handler (move-only, like the underlying Function).
        void Add(Handler handler) { m_handlers.PushBack(Move(handler)); }
        /// Remove all handlers.
        void Clear() noexcept { m_handlers.Clear(); }
        [[nodiscard]] usize Count() const noexcept { return m_handlers.Size(); }

        /// Invoke every handler in registration order.
        void Invoke(Args... args) const { for (const Handler& h : m_handlers) { h(args...); } }
        void operator()(Args... args) const { Invoke(args...); }

    private:
        Array<Handler> m_handlers;
    };
}
