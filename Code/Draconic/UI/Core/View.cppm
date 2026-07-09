// Draconic UI - :view partition
//
// View: the base of the retained-mode view hierarchy (ported from Sedulous.UI/src/Core/View.bf).
//
// SEED: this is a MINIMAL View - just the pieces the Styling stack needs (Object identity for
// selector type-matching + the style-class list / HasClass). View grows incrementally as later
// subsystems land (layout, drawing, input, properties, tree). Object gives Cast<T>; IPropertyOwner
// lets Property<T> notify it.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui:view;

import draconic.core;   // Object, Array, String, StringView
import :enums;          // InvalidationKind
import :property_owner;

using namespace draconic::core;

export namespace draconic::ui
{
    class View : public Object, public IPropertyOwner
    {
        DRACONIC_OBJECT(View, Object)
    public:
        /// Style classes for selector matching. Multiple allowed (AddClass/RemoveClass/HasClass).
        Array<String> StyleClasses;

        View() = default;

        [[nodiscard]] bool HasClass(StringView name) const
        {
            for (const String& cls : StyleClasses) { if (cls == name) { return true; } }
            return false;
        }
        void AddClass(StringView name)
        {
            if (HasClass(name)) { return; }
            StyleClasses.PushBack(String(name));
            Invalidate();
        }
        void RemoveClass(StringView name)
        {
            for (usize i = 0; i < StyleClasses.Size(); ++i)
            {
                if (StyleClasses[i] == name) { StyleClasses.RemoveAt(i); Invalidate(); return; }
            }
        }

        /// Marks the view as needing a redraw. (Seed: sets a flag; full invalidation lands later.)
        void Invalidate() noexcept { m_needsRedraw = true; }
        [[nodiscard]] bool NeedsRedraw() const noexcept { return m_needsRedraw; }

        // IPropertyOwner
        void OnPropertyChanged(InvalidationKind kind) override { (void)kind; Invalidate(); }

    protected:
        bool m_needsRedraw = true;
    };

    DRACONIC_DEFINE_OBJECT(View, "draconic::ui")
}
