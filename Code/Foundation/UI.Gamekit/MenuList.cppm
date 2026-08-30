// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI.Gamekit - :menu_list partition
//
// MenuList: a vertical stack of selectable menu rows, "thin over CORE" - a FlexLayout of focusable
// Buttons whose Up/Down directional-focus neighbours are wired with WRAP-AROUND, so the CORE
// FocusManager drives arrow/pad navigation between rows (wrapping at the ends) with no key handling
// here. Selection == the CORE-focused row; activation (click / Enter / gamepad A) fires
// OnItemActivated. Game-styled via the "menu"/"menu-item" style classes the GameTheme can target.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui.gamekit:menu_list;

import foundation.core;
import foundation.ui;

using namespace foundation::core;

export namespace foundation::ui::gamekit
{
    class MenuList : public foundation::ui::FlexLayout
    {
        RTTI_OBJECT(MenuList, foundation::ui::FlexLayout)
    public:
        MenuList();

        // Append a selectable row (a focusable, full-width, game-styled Button). Returns the row so a
        // caller can style it or wire OnClick directly; the second overload wires onSelect for you.
        foundation::ui::Button* AddItem(StringView label);
        foundation::ui::Button* AddItem(StringView label, Function<void()> onSelect);
        // Remove every row.
        void ClearItems();

        [[nodiscard]] usize ItemCount() const noexcept { return m_items.Size(); }
        [[nodiscard]] foundation::ui::Button* ItemAt(usize index) const noexcept
        {
            return index < m_items.Size() ? m_items[index] : nullptr;
        }

        // The highlighted row == the CORE-focused row (-1 if none, or not attached to a context).
        [[nodiscard]] i32 SelectedIndex() const;
        // Move focus (and thus the highlight) to a row; out-of-range is a no-op.
        void SetSelectedIndex(i32 index);
        // Focus the first row so a pad/keyboard user lands on something (call on screen enter).
        void FocusFirst();

        // Fired when a row is activated (click / Enter / gamepad A), with its index.
        Event<void(MenuList*, i32)> OnItemActivated;

    private:
        // Re-wire each row's Up/Down directional-focus neighbours with wrap-around; the CORE
        // FocusManager then navigates rows on arrow/pad input.
        void RewireFocusChain();

        Array<foundation::ui::Button*> m_items; // rows; OWNED by ViewGroup (m_children), raw here
    };
}
