// Draconic Core - :string_util partition
//
// Small, generic string/character scanning helpers over the UTF-8 StringView: ASCII
// character classification (whitespace / digit / hex) and whitespace trimming. Kept
// separate from :string so the container type stays lean. char8_t-based (the primary
// String encoding); the relevant classes are all ASCII.

module;
#include "Core/Prelude.h"

export module draconic.core:string_util;

import :base;     // char8_t, usize, i32
import :string;   // StringView

export namespace draconic::core
{
    [[nodiscard]] constexpr bool IsWhiteSpace(char8_t c) noexcept
    {
        return c == u8' ' || c == u8'\t' || c == u8'\n' || c == u8'\r' || c == u8'\f' || c == u8'\v';
    }

    [[nodiscard]] constexpr bool IsDigit(char8_t c) noexcept { return c >= u8'0' && c <= u8'9'; }

    [[nodiscard]] constexpr bool IsHexDigit(char8_t c) noexcept
    {
        return IsDigit(c) || (c >= u8'a' && c <= u8'f') || (c >= u8'A' && c <= u8'F');
    }

    // Hex nibble value (0-15), or -1 if `c` is not a hex digit.
    [[nodiscard]] constexpr i32 HexValue(char8_t c) noexcept
    {
        if (c >= u8'0' && c <= u8'9') return static_cast<i32>(c - u8'0');
        if (c >= u8'a' && c <= u8'f') return static_cast<i32>(c - u8'a') + 10;
        if (c >= u8'A' && c <= u8'F') return static_cast<i32>(c - u8'A') + 10;
        return -1;
    }

    [[nodiscard]] constexpr StringView TrimStart(StringView s) noexcept
    {
        usize begin = 0;
        while (begin < s.Size() && IsWhiteSpace(s[begin])) ++begin;
        return s.SubStr(begin, s.Size() - begin);
    }

    [[nodiscard]] constexpr StringView TrimEnd(StringView s) noexcept
    {
        usize end = s.Size();
        while (end > 0 && IsWhiteSpace(s[end - 1])) --end;
        return s.SubStr(0, end);
    }

    // Trim leading and trailing whitespace.
    [[nodiscard]] constexpr StringView Trim(StringView s) noexcept
    {
        usize begin = 0, end = s.Size();
        while (begin < end && IsWhiteSpace(s[begin])) ++begin;
        while (end > begin && IsWhiteSpace(s[end - 1])) --end;
        return s.SubStr(begin, end - begin);
    }
}
