// Draconic UI - :tooltip_placement partition
//
// Where a tooltip appears relative to its anchor view. Ported verbatim from
// Sedulous.UI/src/Overlay/TooltipPlacement.bf.

module;
#include "Core/Prelude.h"

export module draconic.ui:tooltip_placement;

export namespace draconic::ui
{
    /// Where the tooltip appears relative to the anchor view.
    enum class TooltipPlacement
    {
        /// Below the anchor; flips above if clipping bottom.
        Bottom,
        /// Above the anchor; flips below if clipping top.
        Top,
        /// Right of the anchor; flips left if clipping right.
        Right,
        /// Left of the anchor; flips right if clipping left.
        Left
    };
}
