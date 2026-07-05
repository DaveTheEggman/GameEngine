// Draconic Core — :plane partition
//
// Plane: normal·p + d = 0, with signed-distance and normalization.

module;
#include "Core/Prelude.h"

export module draconic.core:plane;

import :base;
import :math;
import :vector3;

export namespace draconic::core
{
    // =======================================================================
    // Plane — normal·p + d = 0.
    // =======================================================================
    struct Plane
    {
        Vector3 normal;
        f32 d;

        [[nodiscard]] static Plane FromPointNormal(Vector3 point, Vector3 unitNormal) noexcept
        {
            return Plane{ unitNormal, -Dot(unitNormal, point) };
        }

        // Signed distance: > 0 in front (normal side), < 0 behind, ~0 on the plane.
        [[nodiscard]] f32 SignedDistance(Vector3 p) const noexcept { return Dot(normal, p) + d; }

        [[nodiscard]] Plane Normalized() const noexcept
        {
            const f32 length = Length(normal);
            if (length <= kEpsilon) { return *this; }
            const f32 inv = 1.0f / length;
            return Plane{ normal * inv, d * inv };
        }
    };
}
