// Raptor Core — :geometry partition
//
// Simple geometric primitives built on the vector types: axis-aligned bounding
// box, plane, and 2D rectangle.

module;
#include "Core/Prelude.h"

export module raptor.core:geometry;

import :base;
import :math;

export namespace raptor::core
{
    // =======================================================================
    // AABB — axis-aligned bounding box.
    // =======================================================================
    struct AABB
    {
        Vec3 min;
        Vec3 max;

        // An inverted box (min > max) so the first Expand sets real bounds.
        [[nodiscard]] static AABB Empty() noexcept
        {
            return AABB{ Vec3{ kFloatMax, kFloatMax, kFloatMax },
                         Vec3{ -kFloatMax, -kFloatMax, -kFloatMax } };
        }

        [[nodiscard]] static AABB FromCenterExtents(Vec3 center, Vec3 extents) noexcept
        {
            return AABB{ center - extents, center + extents };
        }

        [[nodiscard]] Vec3 Center() const noexcept { return (min + max) * 0.5f; }
        [[nodiscard]] Vec3 Size() const noexcept { return max - min; }
        [[nodiscard]] Vec3 Extents() const noexcept { return (max - min) * 0.5f; }
        [[nodiscard]] bool IsValid() const noexcept { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

        [[nodiscard]] bool Contains(Vec3 p) const noexcept
        {
            return p.x >= min.x && p.x <= max.x
                && p.y >= min.y && p.y <= max.y
                && p.z >= min.z && p.z <= max.z;
        }

        [[nodiscard]] bool Intersects(const AABB& other) const noexcept
        {
            return min.x <= other.max.x && max.x >= other.min.x
                && min.y <= other.max.y && max.y >= other.min.y
                && min.z <= other.max.z && max.z >= other.min.z;
        }

        // Grows the box to include a point.
        void Expand(Vec3 p) noexcept
        {
            min = Min(min, p);
            max = Max(max, p);
        }
    };

    [[nodiscard]] inline AABB Merge(const AABB& a, const AABB& b) noexcept
    {
        return AABB{ Min(a.min, b.min), Max(a.max, b.max) };
    }

    // =======================================================================
    // Plane — normal·p + d = 0.
    // =======================================================================
    struct Plane
    {
        Vec3 normal;
        f32 d;

        [[nodiscard]] static Plane FromPointNormal(Vec3 point, Vec3 unitNormal) noexcept
        {
            return Plane{ unitNormal, -Dot(unitNormal, point) };
        }

        // Signed distance: > 0 in front (normal side), < 0 behind, ~0 on the plane.
        [[nodiscard]] f32 SignedDistance(Vec3 p) const noexcept { return Dot(normal, p) + d; }

        [[nodiscard]] Plane Normalized() const noexcept
        {
            const f32 length = Length(normal);
            if (length <= kEpsilon) { return *this; }
            const f32 inv = 1.0f / length;
            return Plane{ normal * inv, d * inv };
        }
    };

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
