// Draconic Core - :vector4 partition
//
// Vector4: 4D f32 vector - arithmetic, Dot/Length/Normalized, XYZ(), component
// constants. Converts from Vector3.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module draconic.core:vector4;

import :base;
import :math;
import :vector3;

export namespace draconic::core
{
    // =======================================================================
    // Vector4
    // =======================================================================
    struct Vector4
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
        f32 w = 0.0f;

        constexpr Vector4() noexcept = default;
        constexpr Vector4(f32 inX, f32 inY, f32 inZ, f32 inW) noexcept : x(inX), y(inY), z(inZ), w(inW) {}
        explicit constexpr Vector4(f32 s) noexcept : x(s), y(s), z(s), w(s) {}
        constexpr Vector4(Vector3 xyz, f32 inW) noexcept : x(xyz.x), y(xyz.y), z(xyz.z), w(inW) {}

        [[nodiscard]] constexpr f32& operator[](usize i) noexcept { DRACONIC_ASSERT(i < 4); return (&x)[i]; }
        [[nodiscard]] constexpr f32 operator[](usize i) const noexcept { DRACONIC_ASSERT(i < 4); return (&x)[i]; }

        [[nodiscard]] constexpr Vector3 XYZ() const noexcept { return { x, y, z }; }

        constexpr Vector4 operator-() const noexcept { return { -x, -y, -z, -w }; }

        constexpr Vector4& operator+=(Vector4 r) noexcept { x += r.x; y += r.y; z += r.z; w += r.w; return *this; }
        constexpr Vector4& operator-=(Vector4 r) noexcept { x -= r.x; y -= r.y; z -= r.z; w -= r.w; return *this; }
        constexpr Vector4& operator*=(f32 s) noexcept { x *= s; y *= s; z *= s; w *= s; return *this; }

        static const Vector4 Zero;
        static const Vector4 One;
    };

    inline constexpr Vector4 Vector4::Zero{ 0.0f, 0.0f, 0.0f, 0.0f };
    inline constexpr Vector4 Vector4::One{ 1.0f, 1.0f, 1.0f, 1.0f };

    [[nodiscard]] constexpr Vector4 operator+(Vector4 a, Vector4 b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w }; }
    [[nodiscard]] constexpr Vector4 operator-(Vector4 a, Vector4 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w }; }
    [[nodiscard]] constexpr Vector4 operator*(Vector4 v, f32 s) noexcept { return { v.x * s, v.y * s, v.z * s, v.w * s }; }
    [[nodiscard]] constexpr Vector4 operator*(f32 s, Vector4 v) noexcept { return { v.x * s, v.y * s, v.z * s, v.w * s }; }
    [[nodiscard]] constexpr Vector4 Lerp(Vector4 a, Vector4 b, f32 t) noexcept { return a + (b - a) * t; }
    [[nodiscard]] constexpr bool operator==(Vector4 a, Vector4 b) noexcept
    {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
    }

    [[nodiscard]] constexpr f32 Dot(Vector4 a, Vector4 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
    [[nodiscard]] constexpr f32 LengthSquared(Vector4 v) noexcept { return Dot(v, v); }
    [[nodiscard]] inline f32 Length(Vector4 v) noexcept { return Sqrt(LengthSquared(v)); }

    [[nodiscard]] inline Vector4 Normalized(Vector4 v) noexcept
    {
        const f32 lengthSq = LengthSquared(v);
        return (lengthSq <= kEpsilon * kEpsilon) ? Vector4::Zero : v * (1.0f / Sqrt(lengthSq));
    }

    [[nodiscard]] inline bool NearlyEqual(Vector4 a, Vector4 b, f32 epsilon = kEpsilon) noexcept
    {
        return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon)
            && NearlyEqual(a.z, b.z, epsilon) && NearlyEqual(a.w, b.w, epsilon);
    }
}
