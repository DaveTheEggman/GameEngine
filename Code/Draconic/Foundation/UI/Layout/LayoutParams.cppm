// Draconic UI - :layout_params partition
//
// Base layout parameters for a view within a container. Container-specific subclasses add fields
// (e.g. FlexLayoutParams adds Grow/Shrink). Ported from Sedulous.UI/src/Layout/LayoutParams.bf.
// Object + RTTI_OBJECT so the layout algorithms can Cast<T> down to their param subclasses.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui:layout_params;

import foundation.core; // Object
import :thickness;
import :size_spec;

using namespace foundation::core;

export namespace foundation::ui
{
    class LayoutParams : public Object
    {
        RTTI_OBJECT(LayoutParams, Object)
    public:
        /// Desired width. Default: Wrap (fit to content).
        SizeSpec Width = SizeSpec::Wrap();
        /// Desired height. Default: Wrap (fit to content).
        SizeSpec Height = SizeSpec::Wrap();
        /// Margin (space between this view and siblings/parent).
        Thickness Margin{};

        LayoutParams() = default;
    };

    RTTI_DEFINE_OBJECT(LayoutParams, "rtti::ui")
}
