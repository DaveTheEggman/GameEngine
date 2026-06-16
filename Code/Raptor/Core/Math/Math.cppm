// Raptor Core — :math partition (scalars + vectors)
//
// Conventions (see Documentation/Planning/Core.md §7): row-major matrices, row
// vectors, XNA-style — established when matrices land. Vectors here are plain
// f32 tuples. Scalar path first; SIMD later.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include <cmath>

export module raptor.core:math;

import :base;

export namespace raptor::core
{
    // =======================================================================
    // Constants & scalar functions
    // =======================================================================
    inline constexpr f32 kPi = 3.14159265358979323846f;
    inline constexpr f32 kTwoPi = 2.0f * kPi;
    inline constexpr f32 kHalfPi = 0.5f * kPi;
    inline constexpr f32 kInvPi = 1.0f / kPi;
    inline constexpr f32 kDegToRad = kPi / 180.0f;
    inline constexpr f32 kRadToDeg = 180.0f / kPi;
    inline constexpr f32 kEpsilon = 1.0e-6f;
    inline constexpr f32 kFloatMax = 3.402823466e38f;

    [[nodiscard]] constexpr f32 Abs(f32 x) noexcept { return x < 0.0f ? -x : x; }
    [[nodiscard]] inline f32 Sqrt(f32 x) noexcept { return std::sqrt(x); }
    [[nodiscard]] inline f32 Sin(f32 x) noexcept { return std::sin(x); }
    [[nodiscard]] inline f32 Cos(f32 x) noexcept { return std::cos(x); }
    [[nodiscard]] inline f32 Tan(f32 x) noexcept { return std::tan(x); }
    [[nodiscard]] inline f32 Asin(f32 x) noexcept { return std::asin(x); }
    [[nodiscard]] inline f32 Acos(f32 x) noexcept { return std::acos(x); }
    [[nodiscard]] inline f32 Atan2(f32 y, f32 x) noexcept { return std::atan2(y, x); }
    [[nodiscard]] inline f32 Floor(f32 x) noexcept { return std::floor(x); }
    [[nodiscard]] inline f32 Ceil(f32 x) noexcept { return std::ceil(x); }
    [[nodiscard]] inline f32 Pow(f32 base, f32 exp) noexcept { return std::pow(base, exp); }

    [[nodiscard]] constexpr f32 DegreesToRadians(f32 degrees) noexcept { return degrees * kDegToRad; }
    [[nodiscard]] constexpr f32 RadiansToDegrees(f32 radians) noexcept { return radians * kRadToDeg; }

    [[nodiscard]] constexpr f32 Lerp(f32 a, f32 b, f32 t) noexcept { return a + (b - a) * t; }

    [[nodiscard]] inline bool NearlyEqual(f32 a, f32 b, f32 epsilon = kEpsilon) noexcept
    {
        return Abs(a - b) <= epsilon;
    }

    [[nodiscard]] inline bool NearlyZero(f32 x, f32 epsilon = kEpsilon) noexcept
    {
        return Abs(x) <= epsilon;
    }

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

    // =======================================================================
    // Color — linear RGBA, float components (typically 0..1).
    // =======================================================================
    struct Color
    {
        f32 r = 0.0f;
        f32 g = 0.0f;
        f32 b = 0.0f;
        f32 a = 1.0f;

        constexpr Color() noexcept = default;
        constexpr Color(f32 inR, f32 inG, f32 inB, f32 inA = 1.0f) noexcept : r(inR), g(inG), b(inB), a(inA) {}

        // Packs to 0xRRGGBBAA (components clamped to 0..1).
        [[nodiscard]] u32 ToRGBA8() const noexcept
        {
            const auto byteOf = [](f32 c) -> u32
            {
                return static_cast<u32>(Clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            return (byteOf(r) << 24) | (byteOf(g) << 16) | (byteOf(b) << 8) | byteOf(a);
        }

        [[nodiscard]] static Color FromRGBA8(u32 packed) noexcept
        {
            return Color{ static_cast<f32>((packed >> 24) & 0xFFu) / 255.0f,
                          static_cast<f32>((packed >> 16) & 0xFFu) / 255.0f,
                          static_cast<f32>((packed >> 8) & 0xFFu) / 255.0f,
                          static_cast<f32>(packed & 0xFFu) / 255.0f };
        }

        static const Color White;
        static const Color Black;
        static const Color Red;
        static const Color Green;
        static const Color Blue;
        static const Color Transparent;
    };

    inline constexpr Color Color::White{ 1.0f, 1.0f, 1.0f, 1.0f };
    inline constexpr Color Color::Black{ 0.0f, 0.0f, 0.0f, 1.0f };
    inline constexpr Color Color::Red{ 1.0f, 0.0f, 0.0f, 1.0f };
    inline constexpr Color Color::Green{ 0.0f, 1.0f, 0.0f, 1.0f };
    inline constexpr Color Color::Blue{ 0.0f, 0.0f, 1.0f, 1.0f };
    inline constexpr Color Color::Transparent{ 0.0f, 0.0f, 0.0f, 0.0f };

    [[nodiscard]] constexpr Color operator*(Color c, f32 s) noexcept { return { c.r * s, c.g * s, c.b * s, c.a * s }; }
    [[nodiscard]] constexpr Color operator+(Color a, Color b) noexcept { return { a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a }; }
    [[nodiscard]] constexpr bool operator==(Color a, Color b) noexcept
    {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    [[nodiscard]] constexpr Color Lerp(Color a, Color b, f32 t) noexcept
    {
        return { Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t) };
    }

    [[nodiscard]] inline bool NearlyEqual(Color a, Color b, f32 epsilon = kEpsilon) noexcept
    {
        return NearlyEqual(a.r, b.r, epsilon) && NearlyEqual(a.g, b.g, epsilon)
            && NearlyEqual(a.b, b.b, epsilon) && NearlyEqual(a.a, b.a, epsilon);
    }

    // =======================================================================
    // Random — PCG32. Deterministic and seedable (good for replays/tests).
    // =======================================================================
    class Random
    {
    public:
        explicit Random(u64 seed = 0x853c49e6748fea9bull, u64 sequence = 0xda3e39cb94b95bdbull) noexcept
        {
            m_state = 0;
            m_inc = (sequence << 1u) | 1u;
            NextU32();
            m_state += seed;
            NextU32();
        }

        u32 NextU32() noexcept
        {
            const u64 old = m_state;
            m_state = old * 6364136223846793005ull + m_inc;
            const u32 xorshifted = static_cast<u32>(((old >> 18u) ^ old) >> 27u);
            const u32 rot = static_cast<u32>(old >> 59u);
            return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
        }

        [[nodiscard]] u64 NextU64() noexcept
        {
            const u64 hi = NextU32();
            const u64 lo = NextU32();
            return (hi << 32u) | lo;
        }

        // Uniform float in [0, 1).
        [[nodiscard]] f32 NextFloat() noexcept
        {
            return static_cast<f32>(NextU32() >> 8u) * (1.0f / 16777216.0f);
        }

        [[nodiscard]] f32 NextFloat(f32 min, f32 max) noexcept
        {
            return min + NextFloat() * (max - min);
        }

        // Uniform integer in [min, max] inclusive.
        [[nodiscard]] i32 NextInt(i32 min, i32 max) noexcept
        {
            const u32 range = static_cast<u32>(max - min) + 1u;
            return min + static_cast<i32>(NextU32() % range);
        }

        [[nodiscard]] bool NextBool() noexcept { return (NextU32() & 1u) != 0u; }

    private:
        u64 m_state;
        u64 m_inc;
    };
}
