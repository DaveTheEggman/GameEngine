// Raptor::FontsTTF — raptor.fonts.ttf:common partition
//
// Small helpers shared by the TTF parser + atlas baker: the supported
// extension list and a case-insensitive extension compare. Sedulous duplicated
// these in both classes; here they live once to avoid cross-TU inline-symbol
// clashes in C++ modules.

module;
#include "Core/Prelude.h"

export module raptor.fonts.ttf:common;

import raptor.core;

using namespace raptor::core;

export namespace raptor::fonts
{
    // ASCII case-insensitive compare for extension matching.
    [[nodiscard]] inline bool ExtEquals(StringView a, StringView b)
    {
        if (a.Size() != b.Size())
            return false;
        for (usize i = 0; i < a.Size(); ++i)
        {
            widechar ca = a[i], cb = b[i];
            if (ca >= u'A' && ca <= u'Z') ca = static_cast<widechar>(ca - u'A' + u'a');
            if (cb >= u'A' && cb <= u'Z') cb = static_cast<widechar>(cb - u'A' + u'a');
            if (ca != cb)
                return false;
        }
        return true;
    }

    // .ttf / .ttc / .otf — the formats the TTF backend handles.
    [[nodiscard]] inline Span<const StringView> TrueTypeExtensions()
    {
        static const StringView exts[] = { StringView(u".ttf"), StringView(u".ttc"), StringView(u".otf") };
        return Span<const StringView>(exts, 3);
    }
}
