// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor App - :tool_panel_widgets partition
//
// The small widget kit every viewport-tool settings panel composes: a vertical panel root, a
// caption row, a segmented row of exclusive toggle buttons (mode / layer choices), a property-grid
// float row and the grid mount that fills the panel. Shared by the terrain brushes (sculpt, splat)
// and the vegetation paint brush, so a new brush panel is a few lines of composition and every
// panel reads the same.

module;
#include "Core/Prelude.h"

export module editor.app:tool_panel_widgets;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core; // EditorRootAllocator

using namespace foundation::core;
namespace ui = foundation::ui;

export namespace editor::app
{
    /// A caption row (a label at `fontSize`).
    [[nodiscard]] inline RefPtr<ui::View> MakeToolPanelRow(StringView title, f32 fontSize)
    {
        auto label = MakeRef<ui::Label>(editor::EditorRootAllocator(), title);
        label->FontSize.SetValue(Optional<f32>{fontSize});
        return label;
    }

    /// The vertical panel root the rows stack into.
    [[nodiscard]] inline RefPtr<ui::FlexLayout> MakeToolPanelRoot()
    {
        auto root = MakeRef<ui::FlexLayout>(editor::EditorRootAllocator());
        root->Direction = ui::Orientation::Vertical;
        root->Spacing = 4.0f;
        return root;
    }

    // A segmented row of exclusive toggle buttons - exactly one lit. contentFor(i) supplies each
    // button's content (icon or label); onSelect(i) applies the choice to the tool; current()
    // returns the selected index for the lit state. Re-lights via SetSilent + Invalidate (no
    // OnCheckedChanged recursion). The buttons capture `this` (this row owns them, so it outlives
    // their closures); the move-only callbacks live as members (never copied into the closures).
    class SegmentedToggle final : public ui::FlexLayout
    {
    public:
        SegmentedToggle()
        {
            Direction = ui::Orientation::Horizontal;
            Spacing = 3.0f;
        }

        void Build(i32 count, Function<RefPtr<ui::View>(i32)> contentFor,
                   Function<void(i32)> onSelect, Function<i32()> current,
                   Function<StringView(i32)> tooltipFor = {})
        {
            m_onSelect = Move(onSelect);
            m_current = Move(current);
            for (i32 i = 0; i < count; ++i)
            {
                auto button = MakeRef<ui::ToggleButton>(MemoryAllocator());
                button->SetContent(contentFor(i));
                if (tooltipFor)
                {
                    button->TooltipText = String(tooltipFor(i));
                }
                SegmentedToggle* self = this;
                button->OnCheckedChanged.Add([self, i](ui::ToggleButton*, bool)
                                             { self->Choose(i); });
                AddView(button.Get());
            }
            Refresh();
        }

        void Choose(i32 i)
        {
            if (m_onSelect)
            {
                m_onSelect(i);
            }
            Refresh();
        }

        void Refresh()
        {
            const i32 selected = m_current ? m_current() : -1;
            for (usize k = 0; k < ChildCount(); ++k)
            {
                if (auto* toggle = Cast<ui::ToggleButton>(GetChildAt(k)))
                {
                    toggle->IsChecked.SetSilent(static_cast<i32>(k) == selected);
                    toggle->Invalidate();
                }
            }
        }

    private:
        Function<void(i32)> m_onSelect;
        Function<i32()> m_current;
    };

    /// A float row on the panel's property grid; returns the editor so the tool can push values
    /// back into it (a wheel-resized radius).
    inline RefPtr<ui::toolkit::FloatEditor> AddToolPanelFloat(ui::toolkit::PropertyGrid& grid,
                                                              StringView label, f64 value, f64 lo,
                                                              f64 hi, f64 step, i32 decimals,
                                                              Function<void(f64)> onChange)
    {
        auto editor = MakeRef<ui::toolkit::FloatEditor>(editor::EditorRootAllocator(), label, value,
                                                        lo, hi, step, decimals, Move(onChange));
        grid.AddProperty(RefPtr<ui::toolkit::PropertyEditor>(editor.Get()));
        return editor;
    }

    /// A bool row on the panel's property grid.
    inline void AddToolPanelBool(ui::toolkit::PropertyGrid& grid, StringView label, bool value,
                                 Function<void(bool)> onChange)
    {
        auto editor = MakeRef<ui::toolkit::BoolEditor>(editor::EditorRootAllocator(), label, value,
                                                       Move(onChange));
        grid.AddProperty(RefPtr<ui::toolkit::PropertyEditor>(editor.Get()));
    }

    /// Mount the grid so it fills the panel body and resizes with the FloatingPanel.
    inline void AddToolPanelGrid(ui::FlexLayout& root, RefPtr<ui::toolkit::PropertyGrid> grid)
    {
        ui::LayoutStyle style;
        style.Width = ui::SizeSpec::Match();
        style.FlexGrow = 1.0f;
        root.AddView(grid.Get(), style);
    }
}
