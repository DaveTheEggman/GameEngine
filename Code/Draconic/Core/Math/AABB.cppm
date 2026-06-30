// Draconic Core — :aabb partition
//
// AABB: axis-aligned bounding box (Min/Max corners) with Contains/
// Intersects/Expand and Merge.

module;
#include "Core/Prelude.h"

export module draconic.core:aabb;

import :base;
import :math;
import :vec3;

export namespace draconic::core
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
}
