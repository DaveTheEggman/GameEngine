// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App - :list_header partition (implementation).
module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module editor.app;

import foundation.core;
import foundation.ui;

using namespace foundation::core;
namespace ui = foundation::ui;

namespace editor::app
{
    ListHeader::ListHeader(StringView title, StringView addTooltip, Function<void()> onAdd)
        : m_onAdd(Move(onAdd))
    {
        Direction = ui::Orientation::Horizontal;
        auto label = MakeRef<ui::Label>(MemoryAllocator(), title);
        label->FontSize.SetValue(12.0f);
        ui::LayoutStyle grow;
        grow.FlexGrow = 1.0f;
        grow.AlignSelf = ui::Align::Center;
        AddView(label.Get(), grow);
        m_add = MakeRef<ui::IconButton>(MemoryAllocator(), EditorIcons::Get().add.Get());
        m_add->TooltipText = String(addTooltip);
        ListHeader* self = this;
        m_add->OnClick.Add(
            [self](ui::ButtonBase*)
            {
                if (self->m_onAdd)
                {
                    self->m_onAdd();
                }
            });
        ui::LayoutStyle center;
        center.AlignSelf = ui::Align::Center;
        AddView(m_add.Get(), center);
    }

    ui::LayoutStyle ListHeader::RowStyle()
    {
        ui::LayoutStyle style;
        style.Width = ui::SizeSpec::Match();
        style.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(22.0f));
        return style;
    }

    RTTI_DEFINE_OBJECT(ListHeader, "rtti::editor::app")
}
