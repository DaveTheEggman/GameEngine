// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI - :layout_style partition
//
// The ONE per-child placement record. Every View carries a LayoutStyle value (View::Layout /
// View::SetLayout); every container reads the fields it understands and ignores the rest, so a
// child keeps its intent across reparenting and a custom container needs no parameter subclass.
// Container-owned data (Grid tracks, Flex direction/justify/align-items, Dock LastChildFill) stays
// on the container. Spec: Documentation/Specs/ui-layout-and-style-model.md, section 1.

module;
#include "Core/Prelude.h"

export module foundation.ui:layout_style;

import foundation.core;
import :thickness;
import :size_spec;
import :gravity;

using namespace foundation::core;

export namespace foundation::ui
{
    /// Dock side for a DockLayout child.
    enum class Dock
    {
        Left,
        Top,
        Right,
        Bottom,
        Fill
    };

    /// Cross-axis alignment (FlexLayout AlignItems and the per-child AlignSelf override).
    enum class Align
    {
        Start,
        End,
        Center,
        Stretch,
        Baseline
    };

    struct LayoutStyle
    {
        // --- every container ---
        /// Desired width. Default: Wrap (fit to content).
        SizeSpec Width = SizeSpec::Wrap();
        /// Desired height. Default: Wrap (fit to content).
        SizeSpec Height = SizeSpec::Wrap();
        /// Space between this view and its siblings/parent.
        Thickness Margin{};

        // --- FlexLayout ---
        /// Extra main-axis space this child absorbs.
        f32 FlexGrow = 0.0f;
        /// How much this child shrinks when space is insufficient.
        f32 FlexShrink = 0.0f;
        /// Cross-axis override (empty = the parent's AlignItems).
        Optional<Align> AlignSelf;

        // --- FrameLayout / FlowLayout / FlexLayout cross-axis ---
        /// Placement anchor within the cell the container hands this child.
        ::foundation::ui::Gravity Gravity = ::foundation::ui::Gravity::None;

        // --- DockLayout ---
        ::foundation::ui::Dock Dock = ::foundation::ui::Dock::Left;

        // --- AbsoluteLayout ---
        /// Offset from the container's content box.
        f32 Left = 0.0f;
        f32 Top = 0.0f;

        // --- GridLayout ---
        /// -1 = auto-flow (the grid assigns the next free cell; the intent stays -1).
        i32 GridRow = -1;
        i32 GridColumn = -1;
        i32 GridRowSpan = 1;
        i32 GridColumnSpan = 1;

        [[nodiscard]] bool operator==(const LayoutStyle& other) const noexcept
        {
            return Width == other.Width && Height == other.Height && Margin == other.Margin &&
                   FlexGrow == other.FlexGrow && FlexShrink == other.FlexShrink &&
                   AlignSelf == other.AlignSelf && Gravity == other.Gravity &&
                   Dock == other.Dock && Left == other.Left && Top == other.Top &&
                   GridRow == other.GridRow && GridColumn == other.GridColumn &&
                   GridRowSpan == other.GridRowSpan && GridColumnSpan == other.GridColumnSpan;
        }
    };
}
