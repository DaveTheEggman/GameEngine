// Raptor Core — :vector partition
//
// Vec2 / Vec3 / Vec4: plain f32 tuples with the usual arithmetic, Dot/Cross/
// Length/Normalized, and component constants. Grouped as one family (they
// convert between each other); built on the :math scalar functions.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module raptor.core:vector;

import :base;
import :math;

export namespace raptor::core
{
    // =======================================================================
    // Vec2
    // =======================================================================
    struct Vec2
    {
        f32 x = 0.0f;
        f32 y = 0.0f;

        constexpr Vec2() noexcept = default;
        constexpr Vec2(f32 inX, f32 inY) noexcept : x(inX), y(inY) {}
        explicit constexpr Vec2(f32 s) noexcept : x(s), y(s) {}

        [[nodiscard]] constexpr f32& operator[](usize i) noexcept { RAPTOR_ASSERT(i < 2); return (&x)[i]; }
        [[nodiscard]] constexpr f32 operator[](usize i) const noexcept { RAPTOR_ASSERT(i < 2); return (&x)[i]; }

        constexpr Vec2 operator-() const noexcept { return { -x, -y }; }

        constexpr Vec2& operator+=(Vec2 r) noexcept { x += r.x; y += r.y; return *this; }
        constexpr Vec2& operator-=(Vec2 r) noexcept { x -= r.x; y -= r.y; return *this; }
        constexpr Vec2& operator*=(f32 s) noexcept { x *= s; y *= s; return *this; }
        constexpr Vec2& operator/=(f32 s) noexcept { x /= s; y /= s; return *this; }

        static const Vec2 Zero;
        static const Vec2 One;
        static const Vec2 UnitX;
        static const Vec2 UnitY;
    };

    inline constexpr Vec2 Vec2::Zero{ 0.0f, 0.0f };
    inline constexpr Vec2 Vec2::One{ 1.0f, 1.0f };
    inline constexpr Vec2 Vec2::UnitX{ 1.0f, 0.0f };
    inline constexpr Vec2 Vec2::UnitY{ 0.0f, 1.0f };

    [[nodiscard]] constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return { a.x + b.x, a.y + b.y }; }
    [[nodiscard]] constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return { a.x - b.x, a.y - b.y }; }
    [[nodiscard]] constexpr Vec2 operator*(Vec2 a, Vec2 b) noexcept { return { a.x * b.x, a.y * b.y }; }
    [[nodiscard]] constexpr Vec2 operator*(Vec2 v, f32 s) noexcept { return { v.x * s, v.y * s }; }
    [[nodiscard]] constexpr Vec2 operator*(f32 s, Vec2 v) noexcept { return { v.x * s, v.y * s }; }
    [[nodiscard]] constexpr Vec2 operator/(Vec2 v, f32 s) noexcept { return { v.x / s, v.y / s }; }
    [[nodiscard]] constexpr bool operator==(Vec2 a, Vec2 b) noexcept { return a.x == b.x && a.y == b.y; }

    [[nodiscard]] constexpr f32 Dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }
    [[nodiscard]] constexpr f32 LengthSquared(Vec2 v) noexcept { return Dot(v, v); }
    [[nodiscard]] inline f32 Length(Vec2 v) noexcept { return Sqrt(LengthSquared(v)); }

    [[nodiscard]] inline Vec2 Normalized(Vec2 v) noexcept
    {
        const f32 lengthSq = LengthSquared(v);
        return (lengthSq <= kEpsilon * kEpsilon) ? Vec2::Zero : v / Sqrt(lengthSq);
    }

    [[nodiscard]] inline bool NearlyEqual(Vec2 a, Vec2 b, f32 epsilon = kEpsilon) noexcept
    {
        return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon);
    }

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
