// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI - :absolute_layout partition
//
// Positions children at explicit X/Y coordinates. Ported from Sedulous.UI/src/Layout/AbsoluteLayout.bf.
// (Beef nested `AbsoluteLayout.LayoutParams` -> top-level `AbsoluteLayoutParams`; the class's own
// child-constraint helper is renamed to avoid hiding ViewGroup::MakeChildConstraints.)

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui:absolute_layout;

import foundation.core; // Max, RefPtr, kFloatMax
import :view;
import :layout_params;
import :box_constraints;
import :size_spec;
import :thickness;

using namespace foundation::core;

export namespace foundation::ui
{
    /// LayoutParams for an AbsoluteLayout child: explicit X/Y.
    class AbsoluteLayoutParams : public LayoutParams
    {
        RTTI_OBJECT(AbsoluteLayoutParams, LayoutParams)
    public:
        f32 X = 0.0f;
        f32 Y = 0.0f;
        AbsoluteLayoutParams() = default;
    };

    class AbsoluteLayout : public ViewGroup
    {
        RTTI_OBJECT(AbsoluteLayout, ViewGroup)
    public:
        AbsoluteLayout() = default;

    protected:
        LayoutParamsPtr CreateDefaultLayoutParams() override
        {
            return MakeRef<AbsoluteLayoutParams>(DefaultAllocator());
        }

        void OnMeasure(BoxConstraints constraints) override
        {
            f32 maxR = 0, maxB = 0;
            for (usize i = 0; i < ChildCount(); ++i)
            {
                View* child = GetChildAt(i);
                if (child->Visibility == Visibility::Gone)
                {
                    continue;
                }

                // Fixed + margin are base-handled here. Wrap children measure unbounded (absolute
                // placement has no natural box), Match fills the content area.
                const f32 availW = Max(0.0f, constraints.MaxWidth - Padding.TotalHorizontal());
                const f32 availH = Max(0.0f, constraints.MaxHeight - Padding.TotalVertical());
                const LayoutParamsPtr& lp = child->LayoutParams;
                const bool fillW = lp && lp->Width.kind == SizeSpec::Kind::Match;
                const bool fillH = lp && lp->Height.kind == SizeSpec::Kind::Match;
                child->Measure(BoxConstraints{fillW ? availW : 0.0f, fillW ? availW : kFloatMax,
                                              fillH ? availH : 0.0f, fillH ? availH : kFloatMax});

                AbsoluteLayoutParams* alp = Cast<AbsoluteLayoutParams>(child->LayoutParams.Get());
                const f32 x = alp != nullptr ? alp->X : 0.0f;
                const f32 y = alp != nullptr ? alp->Y : 0.0f;
                const Float2 mb = child->MarginBoxSize();
                maxR = Max(maxR, x + mb.x);
                maxB = Max(maxB, y + mb.y);
            }
            MeasuredSize = Float2{constraints.ConstrainWidth(maxR + Padding.TotalHorizontal()),
                                  constraints.ConstrainHeight(maxB + Padding.TotalVertical())};
        }

        void OnLayout(f32 left, f32 top, f32 width, f32 height) override
        {
            (void)left;
            (void)top;
            for (usize i = 0; i < ChildCount(); ++i)
            {
                View* child = GetChildAt(i);
                if (child->Visibility == Visibility::Gone)
                {
                    continue;
                }

                const LayoutParamsPtr& lp = child->LayoutParams;
                AbsoluteLayoutParams* alp = Cast<AbsoluteLayoutParams>(lp.Get());
                const f32 ax = alp != nullptr ? alp->X : 0.0f;
                const f32 ay = alp != nullptr ? alp->Y : 0.0f;
                const f32 x = Padding.Left + ax;
                const f32 y = Padding.Top + ay;

                // Margin-box rects (base insets by margin).
                const Float2 mb = child->MarginBoxSize();
                f32 w = mb.x;
                f32 h = mb.y;
                if (lp)
                {
                    if (lp->Width.kind == SizeSpec::Kind::Match)
                        w = Max(0.0f, width - Padding.TotalHorizontal() - ax);
                    if (lp->Height.kind == SizeSpec::Kind::Match)
                        h = Max(0.0f, height - Padding.TotalVertical() - ay);
                }
                child->Layout(x, y, w, h);
            }
        }

    };

    RTTI_DEFINE_OBJECT(AbsoluteLayoutParams, "rtti::ui")
    RTTI_DEFINE_OBJECT(AbsoluteLayout, "rtti::ui")
}
