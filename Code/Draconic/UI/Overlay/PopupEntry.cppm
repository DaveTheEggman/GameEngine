// Draconic UI - :popup_entry partition
//
// Entry tracking a single popup in the PopupLayer. Ported from Sedulous.UI/src/Overlay/PopupEntry.bf.
// The popup is held as a RefPtr<View> (RAII co-ownership while open); the Owner is borrowed. OwnsView is
// kept for API parity - lifetime is governed by ref-counting (drop-on-close destroys iff no other ref).

module;
#include "Core/Prelude.h"

export module draconic.ui:popup_entry;

import draconic.core; // RefPtr
import :view;
import :ipopup_owner;

using namespace draconic::core;

export namespace draconic::ui
{
    struct PopupEntry
    {
        RefPtr<View> Popup;               ///< The popup view.
        IPopupOwner* Owner = nullptr;     ///< Notified when this popup closes (borrowed).
        bool CloseOnClickOutside = false; ///< Clicking outside dismisses it.
        bool IsModal = false;             ///< Blocks input to underlying content.
        bool OwnsView = true;   ///< PopupLayer is the primary owner (delete-on-close semantics).
        f32 X = 0.0f, Y = 0.0f; ///< Position in PopupLayer coordinates.
    };
}
