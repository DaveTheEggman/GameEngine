// UI - :dock_layout partition
//
// Docks children to edges (Left/Top/Right/Bottom/Fill); each docked child claims space from its edge,
// shrinking the remaining area for subsequent children. Ported from Sedulous.UI/src/Layout/DockLayout.bf.
// (Beef nested `DockLayout.LayoutParams` -> top-level `DockLayoutParams`.)

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui:dock_layout;

import foundation.core; // Max
import :view;
import :layout_params;
import :box_constraints;
import :thickness;

using namespace foundation::core;

export namespace foundation::ui
{
    /// Dock position for a DockLayout child.
    enum class Dock
    {
        Left,
        Top,
        Right,
        Bottom,
        Fill
    };

    /// LayoutParams for a DockLayout child.
    class DockLayoutParams : public LayoutParams
    {
        RTTI_OBJECT(DockLayoutParams, LayoutParams)
    public:
        ::foundation::ui::Dock Dock = ::foundation::ui::Dock::Left;
        DockLayoutParams() = default;
        explicit DockLayoutParams(::foundation::ui::Dock dock) : Dock(dock) {}
    };

    class DockLayout : public ViewGroup
    {
        RTTI_OBJECT(DockLayout, ViewGroup)
    public:
        /// When true, the last child fills all remaining space regardless of its Dock.
        bool LastChildFill = false;

        DockLayout() = default;

    protected:
        LayoutParamsPtr CreateDefaultLayoutParams() override
        {
            return MakeRef<DockLayoutParams>(DefaultAllocator());
        }

        void OnMeasure(BoxConstraints constraints) override
        {
            const Thickness chrome = ResolveBoxMetrics().Chrome();
            f32 usedLeft = 0, usedTop = 0, usedRight = 0, usedBottom = 0, maxW = 0, maxH = 0;
            const usize count = ChildCount();

            for (usize i = 0; i < count; ++i)
            {
                View* child = GetChildAt(i);
                if (child->Visibility == Visibility::Gone)
                {
                    continue;
                }

                DockLayoutParams* lp = Cast<DockLayoutParams>(child->LayoutParams.Get());
                const foundation::ui::Dock dock = lp != nullptr ? lp->Dock : foundation::ui::Dock::Left;

                // Margin is base-handled: pass the remaining space as the
                // child's margin-box availability; aggregate margin-box sizes.
                const f32 remainW = Max(0.0f, constraints.MaxWidth - chrome.TotalHorizontal() -
                                                  usedLeft - usedRight);
                const f32 remainH = Max(0.0f, constraints.MaxHeight - chrome.TotalVertical() -
                                                  usedTop - usedBottom);

                const bool isFill =
                    (LastChildFill && i == count - 1) || dock == foundation::ui::Dock::Fill;
                child->Measure(isFill ? BoxConstraints::Tight(remainW, remainH)
                                      : BoxConstraints{0, remainW, 0, remainH});
                const Float2 mb = child->MarginBoxSize();

                switch (dock)
                {
                case foundation::ui::Dock::Left:
                    usedLeft += mb.x;
                    break;
                case foundation::ui::Dock::Right:
                    usedRight += mb.x;
                    break;
                case foundation::ui::Dock::Top:
                    usedTop += mb.y;
                    break;
                case foundation::ui::Dock::Bottom:
                    usedBottom += mb.y;
                    break;
                case foundation::ui::Dock::Fill:
                    break;
                }
                maxW = Max(maxW, usedLeft + usedRight);
                maxH = Max(maxH, usedTop + usedBottom);
            }

            MeasuredSize = Float2{constraints.ConstrainWidth(maxW + Padding.TotalHorizontal()),
                                  constraints.ConstrainHeight(maxH + Padding.TotalVertical())};
        }

        void OnLayout(f32 left, f32 top, f32 width, f32 height) override
        {
            (void)left;
            (void)top;
            const Thickness chrome = ResolveBoxMetrics().Chrome();
            f32 dockLeft = chrome.Left;
            f32 dockTop = chrome.Top;
            f32 dockRight = width - chrome.Right;
            f32 dockBottom = height - chrome.Bottom;
            const usize count = ChildCount();

            for (usize i = 0; i < count; ++i)
            {
                View* child = GetChildAt(i);
                if (child->Visibility == Visibility::Gone)
                {
                    continue;
                }

                DockLayoutParams* lp = Cast<DockLayoutParams>(child->LayoutParams.Get());
                const foundation::ui::Dock dock = lp != nullptr ? lp->Dock : foundation::ui::Dock::Left;
                const bool isFill =
                    (LastChildFill && i == count - 1) || dock == foundation::ui::Dock::Fill;

                // Rects below are MARGIN boxes - the base Layout insets by margin once.
                const Float2 mb = child->MarginBoxSize();
                if (isFill)
                {
                    child->Layout(dockLeft, dockTop, Max(0.0f, dockRight - dockLeft),
                                  Max(0.0f, dockBottom - dockTop));
                    continue;
                }

                switch (dock)
                {
                case foundation::ui::Dock::Left:
                    child->Layout(dockLeft, dockTop, mb.x, Max(0.0f, dockBottom - dockTop));
                    dockLeft += mb.x;
                    break;
                case foundation::ui::Dock::Right:
                    child->Layout(dockRight - mb.x, dockTop, mb.x,
                                  Max(0.0f, dockBottom - dockTop));
                    dockRight -= mb.x;
                    break;
                case foundation::ui::Dock::Top:
                    child->Layout(dockLeft, dockTop, Max(0.0f, dockRight - dockLeft), mb.y);
                    dockTop += mb.y;
                    break;
                case foundation::ui::Dock::Bottom:
                    child->Layout(dockLeft, dockBottom - mb.y,
                                  Max(0.0f, dockRight - dockLeft), mb.y);
                    dockBottom -= mb.y;
                    break;
                case foundation::ui::Dock::Fill:
                    break;
                }
            }
        }
    };

    RTTI_DEFINE_OBJECT(DockLayoutParams, "rtti::ui")
    RTTI_DEFINE_OBJECT(DockLayout, "rtti::ui")
}
