// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App - :compact_asset_slot partition (implementation).
module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module editor.app;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;

using namespace foundation::core;
namespace ui = foundation::ui;

namespace editor::app
{
    CompactAssetSlot::CompactAssetSlot(StringView caption, Span<const StringView> acceptedTypes,
                                       StringView emptyText)
    {
        Direction = ui::Orientation::Horizontal;
        Spacing = 4.0f;
        auto label = MakeRef<ui::Label>(MemoryAllocator(), caption);
        label->FontSize.SetValue(12.0f);
        ui::LayoutStyle center;
        center.AlignSelf = ui::Align::Center;
        AddView(label.Get(), center);
        m_editor = MakeRef<ResourceRefEditor>(MemoryAllocator(), caption, emptyText,
                                              StringView{}, acceptedTypes);
        m_editor->SetEmptyText(emptyText);
    }

    void CompactAssetSlot::Build()
    {
        auto* slot = Cast<AssetPickerSlot>(m_editor->EditorView());
        if (slot != nullptr)
        {
            slot->SetFontSize(12.0f);
        }
        ui::LayoutStyle grow;
        grow.FlexGrow = 1.0f;
        grow.AlignSelf = ui::Align::Center;
        AddView(m_editor->EditorView(), grow);
    }

    RTTI_DEFINE_OBJECT(CompactAssetSlot, "rtti::editor::app")
}
