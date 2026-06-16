module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module raptor.core:color;

import :base;
import :math;

export namespace raptor::core
{
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
}
