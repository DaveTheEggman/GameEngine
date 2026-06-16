// Raptor Core — :vec3 partition
//
// Vec3: 3D f32 vector — arithmetic, Dot/Cross/Length/Normalized, Min/Max,
// Lerp, component constants. Converts from Vec2.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module raptor.core:vec3;

import :base;
import :math;
import :vec2;

export namespace raptor::core
{
    // =======================================================================
    // Vec3
    // =======================================================================
    struct Vec3
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;

        constexpr Vec3() noexcept = default;
        constexpr Vec3(f32 inX, f32 inY, f32 inZ) noexcept : x(inX), y(inY), z(inZ) {}
        explicit constexpr Vec3(f32 s) noexcept : x(s), y(s), z(s) {}
        constexpr Vec3(Vec2 xy, f32 inZ) noexcept : x(xy.x), y(xy.y), z(inZ) {}

        [[nodiscard]] constexpr f32& operator[](usize i) noexcept { RAPTOR_ASSERT(i < 3); return (&x)[i]; }
        [[nodiscard]] constexpr f32 operator[](usize i) const noexcept { RAPTOR_ASSERT(i < 3); return (&x)[i]; }

        constexpr Vec3 operator-() const noexcept { return { -x, -y, -z }; }

        constexpr Vec3& operator+=(Vec3 r) noexcept { x += r.x; y += r.y; z += r.z; return *this; }
        constexpr Vec3& operator-=(Vec3 r) noexcept { x -= r.x; y -= r.y; z -= r.z; return *this; }
        constexpr Vec3& operator*=(f32 s) noexcept { x *= s; y *= s; z *= s; return *this; }
        constexpr Vec3& operator/=(f32 s) noexcept { x /= s; y /= s; z /= s; return *this; }

        static const Vec3 Zero;
        static const Vec3 One;
        static const Vec3 UnitX;
        static const Vec3 UnitY;
        static const Vec3 UnitZ;
    };

    inline constexpr Vec3 Vec3::Zero{ 0.0f, 0.0f, 0.0f };
    inline constexpr Vec3 Vec3::One{ 1.0f, 1.0f, 1.0f };
    inline constexpr Vec3 Vec3::UnitX{ 1.0f, 0.0f, 0.0f };
    inline constexpr Vec3 Vec3::UnitY{ 0.0f, 1.0f, 0.0f };
    inline constexpr Vec3 Vec3::UnitZ{ 0.0f, 0.0f, 1.0f };

    [[nodiscard]] constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    [[nodiscard]] constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    [[nodiscard]] constexpr Vec3 operator*(Vec3 a, Vec3 b) noexcept { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
    [[nodiscard]] constexpr Vec3 operator*(Vec3 v, f32 s) noexcept { return { v.x * s, v.y * s, v.z * s }; }
    [[nodiscard]] constexpr Vec3 operator*(f32 s, Vec3 v) noexcept { return { v.x * s, v.y * s, v.z * s }; }
    [[nodiscard]] constexpr Vec3 operator/(Vec3 v, f32 s) noexcept { return { v.x / s, v.y / s, v.z / s }; }
    [[nodiscard]] constexpr bool operator==(Vec3 a, Vec3 b) noexcept { return a.x == b.x && a.y == b.y && a.z == b.z; }

    [[nodiscard]] constexpr f32 Dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

    [[nodiscard]] constexpr Vec3 Cross(Vec3 a, Vec3 b) noexcept
    {
        return { a.y * b.z - a.z * b.y,
                 a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x };
    }

    [[nodiscard]] constexpr f32 LengthSquared(Vec3 v) noexcept { return Dot(v, v); }
    [[nodiscard]] inline f32 Length(Vec3 v) noexcept { return Sqrt(LengthSquared(v)); }
    [[nodiscard]] inline f32 Distance(Vec3 a, Vec3 b) noexcept { return Length(b - a); }

    // Returns a unit vector, or Zero if the input is near-zero length.
    [[nodiscard]] inline Vec3 Normalized(Vec3 v) noexcept
    {
        const f32 lengthSq = LengthSquared(v);
        if (lengthSq <= kEpsilon * kEpsilon)
        {
            return Vec3::Zero;
        }
        return v / Sqrt(lengthSq);
    }

    [[nodiscard]] constexpr Vec3 Lerp(Vec3 a, Vec3 b, f32 t) noexcept { return a + (b - a) * t; }

    [[nodiscard]] constexpr Vec3 Min(Vec3 a, Vec3 b) noexcept
    {
        return { a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z };
    }
    [[nodiscard]] constexpr Vec3 Max(Vec3 a, Vec3 b) noexcept
    {
        return { a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z };
    }

    [[nodiscard]] inline bool NearlyEqual(Vec3 a, Vec3 b, f32 epsilon = kEpsilon) noexcept
    {
        return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon) && NearlyEqual(a.z, b.z, epsilon);
    }
}
