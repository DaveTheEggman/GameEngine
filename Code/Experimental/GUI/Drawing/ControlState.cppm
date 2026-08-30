// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// GUI - :control_state partition
//
// ControlState: the visual state a stateful drawable (skins, StateListDrawable) selects
// on. Derived from eepp's UI skin states (uistate.hpp); a flat enum - the eepp
// bitmask best-match machinery is not implemented.

module;
#include "Core/Prelude.h"

export module experimental.gui:control_state;

import foundation.core;

using namespace foundation::core;

export namespace experimental::gui
{
    enum class ControlState : u32
    {
        Normal = 0,
        Hover,
        Pressed,
        Focused,
        Disabled,
        Selected,
    };
}
