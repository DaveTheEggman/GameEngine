// Raptor Core — :vec4 partition
//
// Vec4: 4D f32 vector — arithmetic, Dot/Length/Normalized, XYZ(), component
// constants. Converts from Vec3.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module raptor.core:vec4;

import :base;
import :math;
import :vec3;

export namespace raptor::core
{
    // =======================================================================
    // Vec4
    // =======================================================================
    struct Vec4
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
        f32 w = 0.0f;

        constexpr Vec4() noexcept = default;
        constexpr Vec4(f32 inX, f32 inY, f32 inZ, f32 inW) noexcept : x(inX), y(inY), z(inZ), w(inW) {}
        explicit constexpr Vec4(f32 s) noexcept : x(s), y(s), z(s), w(s) {}
        constexpr Vec4(Vec3 xyz, f32 inW) noexcept : x(xyz.x), y(xyz.y), z(xyz.z), w(inW) {}

        [[nodiscard]] constexpr f32& operator[](usize i) noexcept { RAPTOR_ASSERT(i < 4); return (&x)[i]; }
        [[nodiscard]] constexpr f32 operator[](usize i) const noexcept { RAPTOR_ASSERT(i < 4); return (&x)[i]; }

        [[nodiscard]] constexpr Vec3 XYZ() const noexcept { return { x, y, z }; }

        constexpr Vec4 operator-() const noexcept { return { -x, -y, -z, -w }; }

        constexpr Vec4& operator+=(Vec4 r) noexcept { x += r.x; y += r.y; z += r.z; w += r.w; return *this; }
        constexpr Vec4& operator-=(Vec4 r) noexcept { x -= r.x; y -= r.y; z -= r.z; w -= r.w; return *this; }
        constexpr Vec4& operator*=(f32 s) noexcept { x *= s; y *= s; z *= s; w *= s; return *this; }

        static const Vec4 Zero;
        static const Vec4 One;
    };

    inline constexpr Vec4 Vec4::Zero{ 0.0f, 0.0f, 0.0f, 0.0f };
    inline constexpr Vec4 Vec4::One{ 1.0f, 1.0f, 1.0f, 1.0f };

    [[nodiscard]] constexpr Vec4 operator+(Vec4 a, Vec4 b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w }; }
    [[nodiscard]] constexpr Vec4 operator-(Vec4 a, Vec4 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w }; }
    [[nodiscard]] constexpr Vec4 operator*(Vec4 v, f32 s) noexcept { return { v.x * s, v.y * s, v.z * s, v.w * s }; }
    [[nodiscard]] constexpr Vec4 operator*(f32 s, Vec4 v) noexcept { return { v.x * s, v.y * s, v.z * s, v.w * s }; }
    [[nodiscard]] constexpr bool operator==(Vec4 a, Vec4 b) noexcept
    {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
    }

    [[nodiscard]] constexpr f32 Dot(Vec4 a, Vec4 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
    [[nodiscard]] constexpr f32 LengthSquared(Vec4 v) noexcept { return Dot(v, v); }
    [[nodiscard]] inline f32 Length(Vec4 v) noexcept { return Sqrt(LengthSquared(v)); }

    [[nodiscard]] inline Vec4 Normalized(Vec4 v) noexcept
    {
        const f32 lengthSq = LengthSquared(v);
        return (lengthSq <= kEpsilon * kEpsilon) ? Vec4::Zero : v * (1.0f / Sqrt(lengthSq));
    }

    [[nodiscard]] inline bool NearlyEqual(Vec4 a, Vec4 b, f32 epsilon = kEpsilon) noexcept
    {
        return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon)
            && NearlyEqual(a.z, b.z, epsilon) && NearlyEqual(a.w, b.w, epsilon);
    }
}
