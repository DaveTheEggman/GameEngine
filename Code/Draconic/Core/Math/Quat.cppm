// Draconic Core — :quat partition
// Quat: unit quaternion rotation — FromAxisAngle, Hamilton product,
// Conjugate/Dot/Normalized/Slerp, RotateVector, and RotationMatrix (-> Mat4).
//
// Conventions (Documentation/Planning/Core.md §7): row-major storage m[row][col];
// row vectors (v' = v * M); composition left-to-right; XNA-style right-handed
// projections, NDC depth [0,1]; translation in the last row.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module draconic.core:quat;

import :base;
import :math;
import :vec3;
import :mat4;

export namespace draconic::core
{
    // =======================================================================
    // Quat — unit quaternion rotation (x, y, z, w).
    // =======================================================================
    struct Quat
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
        f32 w = 1.0f;

        constexpr Quat() noexcept = default;
        constexpr Quat(f32 inX, f32 inY, f32 inZ, f32 inW) noexcept : x(inX), y(inY), z(inZ), w(inW) {}

        [[nodiscard]] static Quat FromAxisAngle(Vec3 axis, f32 radians) noexcept
        {
            const f32 half = radians * 0.5f;
            const f32 s = Sin(half);
            const Vec3 a = Normalized(axis);
            return Quat{ a.x * s, a.y * s, a.z * s, Cos(half) };
        }

        static const Quat Identity;
    };

    inline constexpr Quat Quat::Identity{ 0.0f, 0.0f, 0.0f, 1.0f };

    // Hamilton product: applies `b` then `a` to a vector.
    [[nodiscard]] constexpr Quat operator*(Quat a, Quat b) noexcept
    {
        return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                 a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                 a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                 a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
    }

    [[nodiscard]] constexpr Quat Conjugate(Quat q) noexcept { return { -q.x, -q.y, -q.z, q.w }; }
    [[nodiscard]] constexpr f32 Dot(Quat a, Quat b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

    // General inverse (= conjugate / |q|^2). For unit quaternions this equals the conjugate.
    [[nodiscard]] inline Quat Inverse(Quat q) noexcept
    {
        const f32 lengthSq = Dot(q, q);
        if (lengthSq <= kEpsilon * kEpsilon) { return Quat::Identity; }
        const f32 inv = 1.0f / lengthSq;
        return { -q.x * inv, -q.y * inv, -q.z * inv, q.w * inv };
    }

    [[nodiscard]] inline Quat Normalized(Quat q) noexcept
    {
        const f32 lengthSq = Dot(q, q);
        if (lengthSq <= kEpsilon * kEpsilon) { return Quat::Identity; }
        const f32 inv = 1.0f / Sqrt(lengthSq);
        return { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
    }

    [[nodiscard]] constexpr Vec3 RotateVector(Quat q, Vec3 v) noexcept
    {
        const Vec3 u{ q.x, q.y, q.z };
        const f32 s = q.w;
        return u * (2.0f * Dot(u, v)) + v * (s * s - Dot(u, u)) + Cross(u, v) * (2.0f * s);
    }

    [[nodiscard]] inline bool NearlyEqual(Quat a, Quat b, f32 epsilon = kEpsilon) noexcept
    {
        return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon)
            && NearlyEqual(a.z, b.z, epsilon) && NearlyEqual(a.w, b.w, epsilon);
    }

    // Spherical linear interpolation along the shortest arc; result is unit.
    [[nodiscard]] inline Quat Slerp(Quat a, Quat b, f32 t) noexcept
    {
        f32 cosTheta = Dot(a, b);
        if (cosTheta < 0.0f) // shortest path
        {
            b = Quat{ -b.x, -b.y, -b.z, -b.w };
            cosTheta = -cosTheta;
        }

        if (cosTheta > 0.9995f) // nearly parallel — lerp + normalize
        {
            return Normalized(Quat{ a.x + (b.x - a.x) * t,
                                    a.y + (b.y - a.y) * t,
                                    a.z + (b.z - a.z) * t,
                                    a.w + (b.w - a.w) * t });
        }

        const f32 theta0 = Acos(cosTheta);
        const f32 theta = theta0 * t;
        const f32 sinTheta = Sin(theta);
        const f32 sinTheta0 = Sin(theta0);
        const f32 s1 = sinTheta / sinTheta0;
        const f32 s0 = Cos(theta) - cosTheta * s1;
        return Quat{ a.x * s0 + b.x * s1,
                     a.y * s0 + b.y * s1,
                     a.z * s0 + b.z * s1,
                     a.w * s0 + b.w * s1 };
    }

    // Rotation matrix for a unit quaternion (row-vector convention, XNA layout).
    [[nodiscard]] constexpr Mat4 RotationMatrix(Quat q) noexcept
    {
        const f32 xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const f32 xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const f32 wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        return Mat4{ { { 1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),        0.0f },
                       { 2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),        0.0f },
                       { 2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy), 0.0f },
                       { 0.0f,                    0.0f,                    0.0f,                    1.0f } } };
    }
}
