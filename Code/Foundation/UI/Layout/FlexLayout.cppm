// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI - :flex_layout partition
//
// CSS Flexbox-inspired container: grow distribution, justify-content, cross-axis alignment. Ported
// from Sedulous.UI/src/Layout/FlexLayout.bf. Per-child FlexGrow/FlexShrink/AlignSelf come from the
// child's LayoutStyle. (Beef ComputeJustify ref params -> f32& out params.)

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui:flex_layout;

import foundation.core; // Max, Optional, RefPtr
import :view;
import :layout_style; // Align, LayoutStyle
import :box_constraints;
import :size_spec;
import :unit;
import :thickness;
import :enums; // Orientation
import :gravity;

using namespace foundation::core;

export namespace foundation::ui
{
    /// Main-axis content distribution.
    enum class Justify
    {
        Start,
        End,
        Center,
        SpaceBetween,
        SpaceAround,
        SpaceEvenly
    };
    class FlexLayout : public ViewGroup
    {
        RTTI_OBJECT(FlexLayout, ViewGroup)
    public:
        Orientation Direction = Orientation::Horizontal;
        Justify JustifyContent = Justify::Start;
        Align AlignItems = Align::Stretch;
        f32 Spacing = 0.0f;

        FlexLayout() = default;

    protected:
        void OnMeasure(BoxConstraints constraints) override
        {
            const BoxConstraints inner = constraints.Deflate(Padding);
            if (Direction == Orientation::Horizontal)
            {
                MeasureHorizontal(inner, constraints);
            }
            else
            {
                MeasureVertical(inner, constraints);
            }
        }

        void OnLayout(f32 left, f32 top, f32 width, f32 height) override
        {
            (void)left;
            (void)top;
            if (Direction == Orientation::Horizontal)
            {
                LayoutHorizontal(width, height);
            }
            else
            {
                LayoutVertical(width, height);
            }
        }

    private:
        static SizeSpec ChildWidth(View* child) { return child->Layout().Width; }
        static SizeSpec ChildHeight(View* child) { return child->Layout().Height; }
        static Thickness ChildMargin(View* child) { return child->Layout().Margin; }
        static f32 Grow(View* child) { return child->Layout().FlexGrow; }

        /// The SEMANTIC part of Flex's first measurement pass, kept parent-side by design:
        /// Match fills the axis, but Match on the CROSS axis is demoted
        /// to loose so it wraps naturally first (a base-side Match would defeat this). Fixed,
        /// margin, and DPI are base-handled.
        static BoxConstraints MakeChildConstraintsLooseCross(BoxConstraints parent, View* child,
                                                             bool isHorizontal)
        {
            const bool fillW = ChildWidth(child).kind == SizeSpec::Kind::Match && !isHorizontal
                                   ? false // cross-axis Match demoted to loose
                                   : ChildWidth(child).kind == SizeSpec::Kind::Match;
            const bool fillH = ChildHeight(child).kind == SizeSpec::Kind::Match && isHorizontal
                                   ? false
                                   : ChildHeight(child).kind == SizeSpec::Kind::Match;
            const f32 availW = Max(0.0f, parent.MaxWidth);
            const f32 availH = Max(0.0f, parent.MaxHeight);
            return BoxConstraints{fillW ? availW : 0.0f, availW, fillH ? availH : 0.0f, availH};
        }

        void MeasureHorizontal(BoxConstraints inner, BoxConstraints outer)
        {
            f32 totalFixed = 0, maxCross = 0, totalGrow = 0;
            i32 visibleCount = 0;
            bool hasMatchCross = false;
            const BoxConstraints looseInner{inner.MinWidth, inner.MaxWidth, 0, inner.MaxHeight};

            for (usize i = 0; i < ChildCount(); ++i)
            {
                View* child = GetChildAt(i);
                if (!IsInFlow(child))
                {
                    continue;
                }
                ++visibleCount;
                const f32 grow = Grow(child);
                if (grow > 0)
                {
                    totalGrow += grow;
                    continue;
                }

                child->Measure(MakeChildConstraintsLooseCross(looseInner, child, true));
                const Float2 mb = child->MarginBoxSize();
                totalFixed += mb.x;
                maxCross = Max(maxCross, mb.y);
                if (ChildHeight(child).kind == SizeSpec::Kind::Match)
                {
                    hasMatchCross = true;
                }
            }
            if (visibleCount > 1)
            {
                totalFixed += Spacing * static_cast<f32>(visibleCount - 1);
            }

            if (totalGrow > 0)
            {
                const bool isMainAxisDefinite = BoxConstraints::IsBounded(inner.MaxWidth);
                const f32 remaining =
                    isMainAxisDefinite ? Max(0.0f, inner.MaxWidth - totalFixed) : 0.0f;
                for (usize i = 0; i < ChildCount(); ++i)
                {
                    View* child = GetChildAt(i);
                    if (!IsInFlow(child))
                    {
                        continue;
                    }
                    const f32 grow = Grow(child);
                    if (grow <= 0)
                    {
                        continue;
                    }
                    if (isMainAxisDefinite)
                    {
                        // The grow share is the child's MARGIN-BOX main length; the base
                        // deflates its own margin from it.
                        const f32 childMain = remaining * grow / totalGrow;
                        child->Measure(BoxConstraints{childMain, Max(0.0f, childMain), 0,
                                                      Max(0.0f, inner.MaxHeight)});
                        totalFixed += childMain;
                    }
                    else
                    {
                        child->Measure(MakeChildConstraintsLooseCross(looseInner, child, true));
                        totalFixed += child->MarginBoxSize().x;
                    }
                    maxCross = Max(maxCross, child->MarginBoxSize().y);
                    if (ChildHeight(child).kind == SizeSpec::Kind::Match)
                    {
                        hasMatchCross = true;
                    }
                }
            }

            if (hasMatchCross && maxCross > 0)
            {
                for (usize i = 0; i < ChildCount(); ++i)
                {
                    View* child = GetChildAt(i);
                    if (!IsInFlow(child))
                    {
                        continue;
                    }
                    if (ChildHeight(child).kind == SizeSpec::Kind::Match)
                    {
                        // Re-measure at the settled line cross size: tight MARGIN-BOX targets
                        // (the base insets to the border box).
                        const Float2 mb = child->MarginBoxSize();
                        child->Measure(BoxConstraints{mb.x, mb.x, maxCross, maxCross});
                    }
                }
            }

            MeasuredSize = Float2{outer.ConstrainWidth(totalFixed + Padding.TotalHorizontal()),
                                  outer.ConstrainHeight(maxCross + Padding.TotalVertical())};
        }

        void MeasureVertical(BoxConstraints inner, BoxConstraints outer)
        {
            f32 totalFixed = 0, maxCross = 0, totalGrow = 0;
            i32 visibleCount = 0;
            bool hasMatchCross = false;
            const BoxConstraints looseInner{0, inner.MaxWidth, inner.MinHeight, inner.MaxHeight};

            for (usize i = 0; i < ChildCount(); ++i)
            {
                View* child = GetChildAt(i);
                if (!IsInFlow(child))
                {
                    continue;
                }
                ++visibleCount;
                const f32 grow = Grow(child);
                if (grow > 0)
                {
                    totalGrow += grow;
                    continue;
                }

                child->Measure(MakeChildConstraintsLooseCross(looseInner, child, false));
                const Float2 mb = child->MarginBoxSize();
                totalFixed += mb.y;
                maxCross = Max(maxCross, mb.x);
                if (ChildWidth(child).kind == SizeSpec::Kind::Match)
                {
                    hasMatchCross = true;
                }
            }
            if (visibleCount > 1)
            {
                totalFixed += Spacing * static_cast<f32>(visibleCount - 1);
            }

            if (totalGrow > 0)
            {
                const bool isMainAxisDefinite = BoxConstraints::IsBounded(inner.MaxHeight);
                const f32 remaining =
                    isMainAxisDefinite ? Max(0.0f, inner.MaxHeight - totalFixed) : 0.0f;
                for (usize i = 0; i < ChildCount(); ++i)
                {
                    View* child = GetChildAt(i);
                    if (!IsInFlow(child))
                    {
                        continue;
                    }
                    const f32 grow = Grow(child);
                    if (grow <= 0)
                    {
                        continue;
                    }
                    if (isMainAxisDefinite)
                    {
                        const f32 childMain = remaining * grow / totalGrow;
                        child->Measure(BoxConstraints{0, Max(0.0f, inner.MaxWidth), childMain,
                                                      Max(0.0f, childMain)});
                        totalFixed += childMain;
                    }
                    else
                    {
                        child->Measure(MakeChildConstraintsLooseCross(looseInner, child, false));
                        totalFixed += child->MarginBoxSize().y;
                    }
                    maxCross = Max(maxCross, child->MarginBoxSize().x);
                    if (ChildWidth(child).kind == SizeSpec::Kind::Match)
                    {
                        hasMatchCross = true;
                    }
                }
            }

            if (hasMatchCross && maxCross > 0)
            {
                for (usize i = 0; i < ChildCount(); ++i)
                {
                    View* child = GetChildAt(i);
                    if (!IsInFlow(child))
                    {
                        continue;
                    }
                    if (ChildWidth(child).kind == SizeSpec::Kind::Match)
                    {
                        const Float2 mb = child->MarginBoxSize();
                        child->Measure(BoxConstraints{maxCross, maxCross, mb.y, mb.y});
                    }
                }
            }

            MeasuredSize = Float2{outer.ConstrainWidth(maxCross + Padding.TotalHorizontal()),
                                  outer.ConstrainHeight(totalFixed + Padding.TotalVertical())};
        }

        void LayoutHorizontal(f32 width, f32 height)
        {
            const f32 contentW = width - Padding.TotalHorizontal();
            const f32 contentH = height - Padding.TotalVertical();

            f32 totalMain = 0;
            i32 visibleCount = 0;
            for (usize i = 0; i < ChildCount(); ++i)
            {
                View* child = GetChildAt(i);
                if (!IsInFlow(child))
                {
                    continue;
                }
                ++visibleCount;
                totalMain += child->MarginBoxSize().x;
            }
            if (visibleCount > 1)
            {
                totalMain += Spacing * static_cast<f32>(visibleCount - 1);
            }

            f32 startOffset = 0, gap = Spacing;
            ComputeJustify(JustifyContent, contentW, totalMain, visibleCount, startOffset, gap);

            f32 xPos = Padding.Left + startOffset;
            bool first = true;
            for (usize i = 0; i < ChildCount(); ++i)
            {
                View* child = GetChildAt(i);
                if (!IsInFlow(child))
                {
                    continue;
                }
                if (!first)
                {
                    xPos += gap;
                }
                first = false;

                const Optional<Align>& alignSelf = child->Layout().AlignSelf;
                const Align align = alignSelf.HasValue() ? alignSelf.Value() : AlignItems;

                // MARGIN-box placement (base insets by margin once).
                const Float2 mb = child->MarginBoxSize();

                f32 yPos = Padding.Top;
                f32 finalH = mb.y;
                switch (align)
                {
                case Align::Start:
                    yPos = Padding.Top;
                    break;
                case Align::End:
                    yPos = Padding.Top + contentH - mb.y;
                    break;
                case Align::Center:
                    yPos = Padding.Top + (contentH - mb.y) * 0.5f;
                    break;
                case Align::Stretch:
                    yPos = Padding.Top;
                    // CSS: stretch fills the cross axis only when the cross size is auto; an
                    // explicit (Fixed) cross size keeps its measured height.
                    finalH = child->Layout().Height->IsFixed() ? mb.y : contentH;
                    break;
                case Align::Baseline:
                    yPos = Padding.Top;
                    break;
                }
                child->Layout(xPos, yPos, mb.x, Max(0.0f, finalH));
                xPos += mb.x;
            }
        }

        void LayoutVertical(f32 width, f32 height)
        {
            const f32 contentW = width - Padding.TotalHorizontal();
            const f32 contentH = height - Padding.TotalVertical();

            f32 totalMain = 0;
            i32 visibleCount = 0;
            for (usize i = 0; i < ChildCount(); ++i)
            {
                View* child = GetChildAt(i);
                if (!IsInFlow(child))
                {
                    continue;
                }
                ++visibleCount;
                totalMain += child->MarginBoxSize().y;
            }
            if (visibleCount > 1)
            {
                totalMain += Spacing * static_cast<f32>(visibleCount - 1);
            }

            f32 startOffset = 0, gap = Spacing;
            ComputeJustify(JustifyContent, contentH, totalMain, visibleCount, startOffset, gap);

            f32 yPos = Padding.Top + startOffset;
            bool first = true;
            for (usize i = 0; i < ChildCount(); ++i)
            {
                View* child = GetChildAt(i);
                if (!IsInFlow(child))
                {
                    continue;
                }
                if (!first)
                {
                    yPos += gap;
                }
                first = false;

                const Optional<Align>& alignSelf = child->Layout().AlignSelf;
                const Align align = alignSelf.HasValue() ? alignSelf.Value() : AlignItems;

                const Float2 mb = child->MarginBoxSize(); // margin-box placement (P2b)

                f32 xPos = Padding.Left;
                f32 finalW = mb.x;
                switch (align)
                {
                case Align::Start:
                    xPos = Padding.Left;
                    break;
                case Align::End:
                    xPos = Padding.Left + contentW - mb.x;
                    break;
                case Align::Center:
                    xPos = Padding.Left + (contentW - mb.x) * 0.5f;
                    break;
                case Align::Stretch:
                    xPos = Padding.Left;
                    finalW = child->Layout().Width->IsFixed() ? mb.x : contentW;
                    break;
                case Align::Baseline:
                    xPos = Padding.Left;
                    break;
                }
                child->Layout(xPos, yPos, Max(0.0f, finalW), mb.y);
                yPos += mb.y;
            }
        }

        static void ComputeJustify(Justify justify, f32 containerSize, f32 totalChildSize,
                                   i32 childCount, f32& startOffset, f32& gap)
        {
            const f32 freeSpace = Max(0.0f, containerSize - totalChildSize);
            switch (justify)
            {
            case Justify::Start:
                break;
            case Justify::End:
                startOffset = freeSpace;
                break;
            case Justify::Center:
                startOffset = freeSpace * 0.5f;
                break;
            case Justify::SpaceBetween:
                if (childCount > 1)
                {
                    gap += freeSpace / static_cast<f32>(childCount - 1);
                }
                break;
            case Justify::SpaceAround:
                if (childCount > 0)
                {
                    const f32 around = freeSpace / static_cast<f32>(childCount);
                    startOffset = around * 0.5f;
                    gap += around;
                }
                break;
            case Justify::SpaceEvenly:
                if (childCount > 0)
                {
                    const f32 even = freeSpace / static_cast<f32>(childCount + 1);
                    startOffset = even;
                    gap += even;
                }
                break;
            }
        }
    };

    RTTI_DEFINE_OBJECT(FlexLayout, "rtti::ui")
}
