// Draconic Core — :transform partition
// Transform: position / rotation / scale, composed as S * R * T into a Matrix4.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module draconic.core:transform;

import :base;
import :vector3;
import :matrix4;
import :quaternion;

export namespace draconic::core
{
    // =======================================================================
    // Transform — position / rotation / scale, composed as S * R * T.
    // =======================================================================
    struct Transform
    {
        Vector3 position = Vector3::Zero;
        Quaternion rotation = Quaternion::Identity;
        Vector3 scale = Vector3::One;

        [[nodiscard]] Matrix4 ToMatrix() const noexcept
        {
            Matrix4 result = Matrix4::Scale(scale) * RotationMatrix(rotation);
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
