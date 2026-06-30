// Draconic Core — :transform partition
// Transform: position / rotation / scale, composed as S * R * T into a Mat4.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module draconic.core:transform;

import :base;
import :vec3;
import :mat4;
import :quat;

export namespace draconic::core
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

        // Component-wise interpolation: position/scale lerp, rotation slerp. (Sedulous BoneTransform.Lerp.)
        [[nodiscard]] static Transform Lerp(const Transform& a, const Transform& b, f32 t) noexcept
        {
            return Transform{
                draconic::core::Lerp(a.position, b.position, t),
                draconic::core::Slerp(a.rotation, b.rotation, t),
                draconic::core::Lerp(a.scale, b.scale, t),
            };
        }
    };

    // Identity transform (position 0, rotation identity, scale 1) — the default-constructed value.
    inline constexpr Transform IdentityTransform{};
}
