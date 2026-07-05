// Draconic::Fonts - :text_util partition
//
// Codepoint iteration over a UTF-8 StringView. Mirrors how Sedulous walked text
// with Beef's `StringView.DecodedChars` (Beef strings are UTF-8); Draconic's
// String is UTF-8 too, so font measuring/shaping decodes UTF-8 sequences here.
// Shared by the baked font and the TTF text shaper.

module;
#include "Core/Prelude.h"

export module draconic.fonts:text_util;

import draconic.core;

using namespace draconic::core;

export namespace draconic::fonts
{
    // Decodes the Unicode codepoint starting at `text[index]`, advancing
    // `index` past the consumed byte(s). A malformed/truncated sequence decodes
    // to U+FFFD (consuming one byte). Returns the codepoint. Caller guarantees
    // `index < text.Size()`.
    [[nodiscard]] inline u32 DecodeCodepoint(StringView text, usize& index) noexcept
    {
        const u8 lead = static_cast<u8>(text[index]);
        ++index;

        u32 codepoint;
        int extra;
        if (lead < 0x80u) { return lead; }
        else if ((lead & 0xE0u) == 0xC0u) { codepoint = lead & 0x1Fu; extra = 1; }
        else if ((lead & 0xF0u) == 0xE0u) { codepoint = lead & 0x0Fu; extra = 2; }
        else if ((lead & 0xF8u) == 0xF0u) { codepoint = lead & 0x07u; extra = 3; }
        else { return 0xFFFDu; } // invalid lead byte

        for (int k = 0; k < extra; ++k)
        {
            if (index >= text.Size()) { return 0xFFFDu; }
            const u8 cont = static_cast<u8>(text[index]);
            if ((cont & 0xC0u) != 0x80u) { return 0xFFFDu; } // not a continuation byte
            codepoint = (codepoint << 6) | (cont & 0x3Fu);
            ++index;
        }
        return codepoint;
    }
}
