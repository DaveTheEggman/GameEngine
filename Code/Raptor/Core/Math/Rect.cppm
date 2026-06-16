// Raptor Core — :rect partition
//
// Rect: 2D rectangle (x,y is the min corner) with Contains/Intersects.

module;
#include "Core/Prelude.h"

export module raptor.core:rect;

import :base;
import :vec2;

export namespace raptor::core
{
    // =======================================================================
    // Rect — 2D rectangle (x, y is the min corner).
    // =======================================================================
    struct Rect
    {
        f32 x;
        f32 y;
        f32 width;
        f32 height;

        [[nodiscard]] Vec2 Min() const noexcept { return Vec2{ x, y }; }
        [[nodiscard]] Vec2 Max() const noexcept { return Vec2{ x + width, y + height }; }
        [[nodiscard]] Vec2 Center() const noexcept { return Vec2{ x + width * 0.5f, y + height * 0.5f }; }

        [[nodiscard]] bool Contains(Vec2 p) const noexcept
        {
            return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height;
        }

        [[nodiscard]] bool Intersects(const Rect& other) const noexcept
        {
            return x <= other.x + other.width && x + width >= other.x
                && y <= other.y + other.height && y + height >= other.y;
        }
    };
}
