// Raptor::Fonts — :text_util partition
//
// Codepoint iteration over Raptor's wide (UTF-16) WideStringView. Sedulous worked
// in UTF-8 and used Beef's `WideStringView.DecodedChars`; Raptor strings are wide,
// so font measuring/shaping decodes surrogate pairs here. Shared by the baked
// font and the TTF text shaper.

module;
#include "Core/Prelude.h"

export module raptor.fonts:text_util;

import raptor.core;

using namespace raptor::core;

export namespace raptor::fonts
{
    // Decodes the Unicode codepoint starting at `text[index]`, advancing
    // `index` past the consumed code unit(s). Combines a valid high/low
    // surrogate pair; an unpaired surrogate decodes to U+FFFD. Returns the
    // codepoint. Caller guarantees `index < text.Size()`.
    [[nodiscard]] inline u32 DecodeCodepoint(WideStringView text, usize& index) noexcept
    {
        const u32 unit = static_cast<u16>(text[index]);
        ++index;

        if (unit >= 0xD800u && unit <= 0xDBFFu)
        {
            if (index < text.Size())
            {
                const u32 low = static_cast<u16>(text[index]);
                if (low >= 0xDC00u && low <= 0xDFFFu)
                {
                    ++index;
                    return 0x10000u + ((unit - 0xD800u) << 10) + (low - 0xDC00u);
                }
            }
            return 0xFFFDu; // unpaired high surrogate
        }
        if (unit >= 0xDC00u && unit <= 0xDFFFu)
        {
            return 0xFFFDu; // unpaired low surrogate
        }
        return unit;
    }
}
