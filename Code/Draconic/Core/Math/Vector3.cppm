// Draconic Core - :vector3 partition
//
// Vector3: 3D f32 vector - arithmetic, Dot/Cross/Length/Normalized, Min/Max,
// Lerp, component constants. Converts from Vector2.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module draconic.core:vector3;

import :base;
import :math;
import :vector2;

export namespace draconic::core
{
    // =======================================================================
    // Vector3
    // =======================================================================
    struct Vector3
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;

        constexpr Vector3() noexcept = default;
        constexpr Vector3(f32 inX, f32 inY, f32 inZ) noexcept : x(inX), y(inY), z(inZ) {}
        explicit constexpr Vector3(f32 s) noexcept : x(s), y(s), z(s) {}
        constexpr Vector3(Vector2 xy, f32 inZ) noexcept : x(xy.x), y(xy.y), z(inZ) {}

        [[nodiscard]] constexpr f32& operator[](usize i) noexcept { DRACONIC_ASSERT(i < 3); return (&x)[i]; }
        [[nodiscard]] constexpr f32 operator[](usize i) const noexcept { DRACONIC_ASSERT(i < 3); return (&x)[i]; }

        constexpr Vector3 operator-() const noexcept { return { -x, -y, -z }; }

        constexpr Vector3& operator+=(Vector3 r) noexcept { x += r.x; y += r.y; z += r.z; return *this; }
        constexpr Vector3& operator-=(Vector3 r) noexcept { x -= r.x; y -= r.y; z -= r.z; return *this; }
        constexpr Vector3& operator*=(f32 s) noexcept { x *= s; y *= s; z *= s; return *this; }
        constexpr Vector3& operator/=(f32 s) noexcept { x /= s; y /= s; z /= s; return *this; }

        static const Vector3 Zero;
        static const Vector3 One;
        static const Vector3 UnitX;
        static const Vector3 UnitY;
        static const Vector3 UnitZ;
    };

    inline constexpr Vector3 Vector3::Zero{ 0.0f, 0.0f, 0.0f };
    inline constexpr Vector3 Vector3::One{ 1.0f, 1.0f, 1.0f };
    inline constexpr Vector3 Vector3::UnitX{ 1.0f, 0.0f, 0.0f };
    inline constexpr Vector3 Vector3::UnitY{ 0.0f, 1.0f, 0.0f };
    inline constexpr Vector3 Vector3::UnitZ{ 0.0f, 0.0f, 1.0f };

    [[nodiscard]] constexpr Vector3 operator+(Vector3 a, Vector3 b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    [[nodiscard]] constexpr Vector3 operator-(Vector3 a, Vector3 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    [[nodiscard]] constexpr Vector3 operator*(Vector3 a, Vector3 b) noexcept { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
    [[nodiscard]] constexpr Vector3 operator*(Vector3 v, f32 s) noexcept { return { v.x * s, v.y * s, v.z * s }; }
    [[nodiscard]] constexpr Vector3 operator*(f32 s, Vector3 v) noexcept { return { v.x * s, v.y * s, v.z * s }; }
    [[nodiscard]] constexpr Vector3 operator/(Vector3 v, f32 s) noexcept { return { v.x / s, v.y / s, v.z / s }; }
    [[nodiscard]] constexpr Vector3 operator/(Vector3 a, Vector3 b) noexcept { return { a.x / b.x, a.y / b.y, a.z / b.z }; }   // component-wise
    [[nodiscard]] constexpr bool operator==(Vector3 a, Vector3 b) noexcept { return a.x == b.x && a.y == b.y && a.z == b.z; }

    [[nodiscard]] constexpr f32 Dot(Vector3 a, Vector3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

    [[nodiscard]] constexpr Vector3 Cross(Vector3 a, Vector3 b) noexcept
    {
        return { a.y * b.z - a.z * b.y,
                 a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x };
    }

    [[nodiscard]] constexpr f32 LengthSquared(Vector3 v) noexcept { return Dot(v, v); }
    [[nodiscard]] inline f32 Length(Vector3 v) noexcept { return Sqrt(LengthSquared(v)); }
    [[nodiscard]] inline f32 Distance(Vector3 a, Vector3 b) noexcept { return Length(b - a); }

    // Returns a unit vector, or Zero if the input is near-zero length.
    [[nodiscard]] inline Vector3 Normalized(Vector3 v) noexcept
    {
        const f32 lengthSq = LengthSquared(v);
        if (lengthSq <= kEpsilon * kEpsilon)
        {
            return Vector3::Zero;
        }
        return v / Sqrt(lengthSq);
    }

    [[nodiscard]] constexpr Vector3 Lerp(Vector3 a, Vector3 b, f32 t) noexcept { return a + (b - a) * t; }

    [[nodiscard]] constexpr Vector3 Min(Vector3 a, Vector3 b) noexcept
    {
        return { a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z };
    }
    [[nodiscard]] constexpr Vector3 Max(Vector3 a, Vector3 b) noexcept
    {
        return { a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z };
    }

    [[nodiscard]] inline bool NearlyEqual(Vector3 a, Vector3 b, f32 epsilon = kEpsilon) noexcept
    {
        return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon) && NearlyEqual(a.z, b.z, epsilon);
    }
}
