// Raptor Core — :transform partition
// Transform: position / rotation / scale, composed as S * R * T into a Mat4.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module raptor.core:transform;

import :base;
import :vec3;
import :mat4;
import :quat;

export namespace raptor::core
{
    // =======================================================================
    // Transform — position / rotation / scale, composed as S * R * T.
    // =======================================================================
    struct Transform
    {
        Vec3 position = Vec3::Zero;
        Quat rotation = Quat::Identity;
        Vec3 scale = Vec3::One;

        [[nodiscard]] Mat4 ToMatrix() const noexcept
        {
            Mat4 result = Mat4::Scale(scale) * RotationMatrix(rotation);
            result.m[3][0] = position.x;
            result.m[3][1] = position.y;
            result.m[3][2] = position.z;
            return result;
        }
    };
}
