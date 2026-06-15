// Raptor Core — :base partition
//
// The foundation: fundamental exported types and a handful of widely-used
// utilities. Lives at the Core root (no dedicated folder). Macros live in
// Prelude.h, not here — modules cannot export macros.

module;
#include "Core/Prelude.h"
#include <cstdint>
#include <cstddef>

export module raptor.core:base;

export namespace raptor::core
{
    // -----------------------------------------------------------------------
    // Fundamental integer / floating types
    // -----------------------------------------------------------------------
    using i8  = std::int8_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;

    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;

    using f32 = float;
    using f64 = double;

    using usize = std::size_t;
    using isize = std::ptrdiff_t;

    using byte = std::byte;

    // Wide character unit for the primary String type (UTF-16); UTF8String uses
    // char8_t. See Documentation/Planning/Core.md §4.5 / §7.
    using widechar = char16_t;
    using utf8char = char8_t;

    // -----------------------------------------------------------------------
    // Small, universally useful utilities
    // -----------------------------------------------------------------------
    template <typename T>
    [[nodiscard]] constexpr const T& Min(const T& a, const T& b)
    {
        return (b < a) ? b : a;
    }

    template <typename T>
    [[nodiscard]] constexpr const T& Max(const T& a, const T& b)
    {
        return (a < b) ? b : a;
    }

    template <typename T>
    [[nodiscard]] constexpr const T& Clamp(const T& v, const T& lo, const T& hi)
    {
        return (v < lo) ? lo : (hi < v) ? hi : v;
    }

    template <typename T, usize N>
    [[nodiscard]] constexpr usize ArrayCount(const T (&)[N]) noexcept
    {
        return N;
    }
}
