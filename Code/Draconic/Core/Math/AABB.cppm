// Draconic Core - :aabb partition
//
// AABB: axis-aligned bounding box (Min/Max corners) with Contains/
// Intersects/Expand and Merge.

module;
#include "Core/Prelude.h"

export module draconic.core:aabb;

import :base;
import :math;
import :vector3;

export namespace draconic::core
{
    // =======================================================================
    // AABB - axis-aligned bounding box.
    // =======================================================================
    struct AABB
    {
        Vector3 min;
        Vector3 max;

        // An inverted box (min > max) so the first Expand sets real bounds.
        [[nodiscard]] static AABB Empty() noexcept
        {
            return AABB{ Vector3{ kFloatMax, kFloatMax, kFloatMax },
                         Vector3{ -kFloatMax, -kFloatMax, -kFloatMax } };
        }

        [[nodiscard]] static AABB FromCenterExtents(Vector3 center, Vector3 extents) noexcept
        {
            return AABB{ center - extents, center + extents };
        }

        [[nodiscard]] Vector3 Center() const noexcept { return (min + max) * 0.5f; }
        [[nodiscard]] Vector3 Size() const noexcept { return max - min; }
        [[nodiscard]] Vector3 Extents() const noexcept { return (max - min) * 0.5f; }
        [[nodiscard]] bool IsValid() const noexcept { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

        [[nodiscard]] bool Contains(Vector3 p) const noexcept
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
        void Expand(Vector3 p) noexcept
        {
            min = Min(min, p);
            max = Max(max, p);
        }
    };

    [[nodiscard]] inline AABB Merge(const AABB& a, const AABB& b) noexcept
    {
        return AABB{ Min(a.min, b.min), Max(a.max, b.max) };
    }
}
