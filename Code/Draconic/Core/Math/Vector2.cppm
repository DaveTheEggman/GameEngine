// Draconic Core — :vector2 partition
//
// Vector2: 2D f32 vector — arithmetic, Dot/Length/Normalized, component
// constants. Built on the :math scalar functions.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module draconic.core:vector2;

import :base;
import :math;

export namespace draconic::core
{
    // =======================================================================
    // Vector2
    // =======================================================================
    struct Vector2
    {
        f32 x = 0.0f;
        f32 y = 0.0f;

        constexpr Vector2() noexcept = default;
        constexpr Vector2(f32 inX, f32 inY) noexcept : x(inX), y(inY) {}
        explicit constexpr Vector2(f32 s) noexcept : x(s), y(s) {}

        [[nodiscard]] constexpr f32& operator[](usize i) noexcept { DRACONIC_ASSERT(i < 2); return (&x)[i]; }
        [[nodiscard]] constexpr f32 operator[](usize i) const noexcept { DRACONIC_ASSERT(i < 2); return (&x)[i]; }

        constexpr Vector2 operator-() const noexcept { return { -x, -y }; }

        constexpr Vector2& operator+=(Vector2 r) noexcept { x += r.x; y += r.y; return *this; }
        constexpr Vector2& operator-=(Vector2 r) noexcept { x -= r.x; y -= r.y; return *this; }
        constexpr Vector2& operator*=(f32 s) noexcept { x *= s; y *= s; return *this; }
        constexpr Vector2& operator/=(f32 s) noexcept { x /= s; y /= s; return *this; }

        static const Vector2 Zero;
        static const Vector2 One;
        static const Vector2 UnitX;
        static const Vector2 UnitY;
    };

    inline constexpr Vector2 Vector2::Zero{ 0.0f, 0.0f };
    inline constexpr Vector2 Vector2::One{ 1.0f, 1.0f };
    inline constexpr Vector2 Vector2::UnitX{ 1.0f, 0.0f };
    inline constexpr Vector2 Vector2::UnitY{ 0.0f, 1.0f };

    [[nodiscard]] constexpr Vector2 operator+(Vector2 a, Vector2 b) noexcept { return { a.x + b.x, a.y + b.y }; }
    [[nodiscard]] constexpr Vector2 operator-(Vector2 a, Vector2 b) noexcept { return { a.x - b.x, a.y - b.y }; }
    [[nodiscard]] constexpr Vector2 operator*(Vector2 a, Vector2 b) noexcept { return { a.x * b.x, a.y * b.y }; }
    [[nodiscard]] constexpr Vector2 operator*(Vector2 v, f32 s) noexcept { return { v.x * s, v.y * s }; }
    [[nodiscard]] constexpr Vector2 operator*(f32 s, Vector2 v) noexcept { return { v.x * s, v.y * s }; }
    [[nodiscard]] constexpr Vector2 operator/(Vector2 v, f32 s) noexcept { return { v.x / s, v.y / s }; }
    [[nodiscard]] constexpr bool operator==(Vector2 a, Vector2 b) noexcept { return a.x == b.x && a.y == b.y; }

    [[nodiscard]] constexpr f32 Dot(Vector2 a, Vector2 b) noexcept { return a.x * b.x + a.y * b.y; }
    [[nodiscard]] constexpr f32 LengthSquared(Vector2 v) noexcept { return Dot(v, v); }
    [[nodiscard]] inline f32 Length(Vector2 v) noexcept { return Sqrt(LengthSquared(v)); }

    [[nodiscard]] inline Vector2 Normalized(Vector2 v) noexcept
    {
        const f32 lengthSq = LengthSquared(v);
        return (lengthSq <= kEpsilon * kEpsilon) ? Vector2::Zero : v / Sqrt(lengthSq);
    }

    [[nodiscard]] constexpr f32 DistanceSquared(Vector2 a, Vector2 b) noexcept { return LengthSquared(b - a); }
    [[nodiscard]] inline f32 Distance(Vector2 a, Vector2 b) noexcept { return Length(b - a); }

    [[nodiscard]] constexpr Vector2 Lerp(Vector2 a, Vector2 b, f32 t) noexcept { return a + (b - a) * t; }

    [[nodiscard]] inline bool NearlyEqual(Vector2 a, Vector2 b, f32 epsilon = kEpsilon) noexcept
    {
        return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon);
    }
}
