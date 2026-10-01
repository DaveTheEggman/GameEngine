// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor App - :list_header partition
//
// ListHeader: the header over a navigation list or tree (a graph's layers, a bus layout, an
// effect's systems) - its title, and the add icon on the right, the same icon a
// ContainerListEditor's header carries (editor-lists-and-asset-slots.md P2).
module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:list_header;

import foundation.core;
import foundation.ui;

using namespace foundation::core;
namespace ui = foundation::ui;

export namespace editor::app
{
    class ListHeader final : public ui::FlexLayout
    {
        RTTI_OBJECT(ListHeader, ui::FlexLayout)
    public:
        ListHeader(StringView title, StringView addTooltip, Function<void()> onAdd);

        /// The add icon, for a caller that enables it only while there is room.
        [[nodiscard]] ui::IconButton* AddButton() const noexcept { return m_add.Get(); }

        /// The layout a header takes in its column: full width, one row high.
        [[nodiscard]] static ui::LayoutStyle RowStyle();

    private:
        Function<void()> m_onAdd;
        RefPtr<ui::IconButton> m_add;
    };
}
