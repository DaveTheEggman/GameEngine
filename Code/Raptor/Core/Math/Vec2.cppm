// Raptor Core — :vec2 partition
//
// Vec2: 2D f32 vector — arithmetic, Dot/Length/Normalized, component
// constants. Built on the :math scalar functions.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module raptor.core:vec2;

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

    [[nodiscard]] constexpr f32 DistanceSquared(Vec2 a, Vec2 b) noexcept { return LengthSquared(b - a); }
    [[nodiscard]] inline f32 Distance(Vec2 a, Vec2 b) noexcept { return Length(b - a); }

    [[nodiscard]] constexpr Vec2 Lerp(Vec2 a, Vec2 b, f32 t) noexcept { return a + (b - a) * t; }

    [[nodiscard]] inline bool NearlyEqual(Vec2 a, Vec2 b, f32 epsilon = kEpsilon) noexcept
    {
        return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon);
    }
}
