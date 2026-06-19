// Raptor Core — :guid partition
//
// Guid: a 128-bit globally unique identifier (asset ids, object ids, ...).
// Generate() produces an RFC 4122 version-4 (random) GUID from a caller-owned
// PRNG, so ids are deterministic/replayable when the generator is seeded.
// Trivially copyable, so it hashes out of the box (generic Hash<T> -> bytes)
// and works as a HashMap/HashSet key.

module;
#include "Core/Prelude.h"

export module raptor.core:guid;

import :base;
import :random;
import :string;

export namespace raptor::core
{
    struct Guid
    {
        u64 high = 0;
        u64 low = 0;

        [[nodiscard]] constexpr bool IsNil() const noexcept { return high == 0 && low == 0; }
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return !IsNil(); }

        // Random (version-4) GUID. The caller owns the generator: seed it for
        // reproducible ids, or share one device for unique ids.
        [[nodiscard]] static Guid Generate(Random& rng) noexcept
        {
            Guid g{ rng.NextU64(), rng.NextU64() };
            // Set version (4) and variant (RFC 4122, 10xx) bits.
            g.high = (g.high & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
            g.low = (g.low & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
            return g;
        }

        // Canonical lowercase 8-4-4-4-12 form into `out` (36 chars + null).
        void ToChars(widechar out[37]) const noexcept
        {
            static constexpr widechar kHex[] = u"0123456789abcdef";
            usize pos = 0;
            for (usize i = 0; i < 16; ++i)
            {
                if (i == 4 || i == 6 || i == 8 || i == 10) { out[pos++] = u'-'; }
                const u64 source = (i < 8) ? high : low;
                const u32 shift = static_cast<u32>((7 - (i & 7)) * 8);
                const u8 byte = static_cast<u8>((source >> shift) & 0xFFull);
                out[pos++] = kHex[byte >> 4];
                out[pos++] = kHex[byte & 0xF];
            }
            out[pos] = u'\0';
        }

        // Parses the canonical 36-char form. Returns false (leaving `out`
        // untouched) on malformed input.
        [[nodiscard]] static bool TryParse(WideStringView text, Guid& out) noexcept
        {
            if (text.Size() != 36) { return false; }

            u64 hi = 0;
            u64 lo = 0;
            usize nibbles = 0;
            for (usize i = 0; i < 36; ++i)
            {
                const widechar c = text[i];
                if (i == 8 || i == 13 || i == 18 || i == 23)
                {
                    if (c != u'-') { return false; }
                    continue;
                }
                const i32 value = HexValue(c);
                if (value < 0) { return false; }
                if (nibbles < 16) { hi = (hi << 4) | static_cast<u64>(value); }
                else { lo = (lo << 4) | static_cast<u64>(value); }
                ++nibbles;
            }
            if (nibbles != 32) { return false; }

            out = Guid{ hi, lo };
            return true;
        }

        static const Guid Nil;

    private:
        [[nodiscard]] static constexpr i32 HexValue(widechar c) noexcept
        {
            if (c >= u'0' && c <= u'9') { return c - u'0'; }
            if (c >= u'a' && c <= u'f') { return c - u'a' + 10; }
            if (c >= u'A' && c <= u'F') { return c - u'A' + 10; }
            return -1;
        }
    };

    inline constexpr Guid Guid::Nil{ 0, 0 };

    [[nodiscard]] constexpr bool operator==(Guid a, Guid b) noexcept { return a.high == b.high && a.low == b.low; }
    [[nodiscard]] constexpr bool operator!=(Guid a, Guid b) noexcept { return !(a == b); }
    [[nodiscard]] constexpr bool operator<(Guid a, Guid b) noexcept
    {
        return (a.high != b.high) ? (a.high < b.high) : (a.low < b.low);
    }
}
