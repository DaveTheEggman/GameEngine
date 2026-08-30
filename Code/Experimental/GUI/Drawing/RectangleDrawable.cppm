// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// GUI - :rectangle_drawable partition
//
// RectangleDrawable: solid-color fill, sharp or rounded. The widget background primitive.
// Derived from eepp's RectangleDrawable / UIBackgroundDrawable (which hand-tessellate a
// TRIANGLE_FAN for rounded corners); here it maps to VG's FillRect / FillRoundedRect.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module experimental.gui:rectangle_drawable;

import foundation.core; // Color
import foundation.vg;   // CornerRadii
import :rect;
import :draw_context;
import :drawable;

using namespace foundation::core;
namespace vg = foundation::vg;

export namespace experimental::gui
{
    class RectangleDrawable : public Drawable
    {
        RTTI_OBJECT(RectangleDrawable, Drawable)
    public:
        RectangleDrawable() = default;
        explicit RectangleDrawable(Color color) noexcept { m_color = color; }

        void SetCornerRadii(vg::CornerRadii radii) noexcept { m_radii = radii; }
        [[nodiscard]] vg::CornerRadii GetCornerRadii() const noexcept { return m_radii; }

        void Draw(DrawContext& ctx, const Rect& dest) override
        {
            if (IsSharp(m_radii))
                ctx.VG().FillRect(dest.ToRectangle(), m_color);
            else
                ctx.VG().FillRoundedRect(dest.ToRectangle(), m_radii, m_color);
        }

    private:
        [[nodiscard]] static bool IsSharp(const vg::CornerRadii& r) noexcept
        {
            return r.topLeft == 0.0f && r.topRight == 0.0f && r.bottomRight == 0.0f &&
                   r.bottomLeft == 0.0f;
        }

        vg::CornerRadii m_radii{};
    };

    RTTI_DEFINE_OBJECT(RectangleDrawable, "rtti::gui")
}
