// Draconic GUI - :css_values partition
//
// CSS value parsers: turn declaration value strings into typed values (color / length /
// bool / thickness). Ported from eepp's css value parsing (common subset). Number parsing
// is a small local scanner (core has no general string->float). Colors: named, #hex
// (#rgb/#rrggbb/#rrggbbaa), and rgb()/rgba().

module;
#include "Core/Prelude.h"

export module draconic.gui:css_values;

import draconic.core;   // Color, Optional, StringView, f32
import draconic.vg;     // CornerRadii
import :thickness;

using namespace draconic::core;
namespace core = draconic::core;
namespace vg = draconic::vg;

export namespace draconic::gui
{
    [[nodiscard]] inline Optional<Color> ParseNamedColor(core::StringView s); // defined below

    // Parse a decimal number (ignores a trailing unit like "px"/"%"). Empty if none.
    [[nodiscard]] inline Optional<f32> ParseLength(core::StringView value)
    {
        const core::StringView s = Trim(value);
        if (s.Size() == 0) return {};
        usize i = 0;
        f32 sign = 1.0f;
        if (s[i] == u8'+') ++i;
        else if (s[i] == u8'-') { sign = -1.0f; ++i; }

        f64 whole = 0.0;
        bool any = false;
        while (i < s.Size() && IsDigit(s[i])) { whole = whole * 10.0 + static_cast<f64>(s[i] - u8'0'); ++i; any = true; }
        f64 frac = 0.0, scale = 1.0;
        if (i < s.Size() && s[i] == u8'.')
        {
            ++i;
            while (i < s.Size() && IsDigit(s[i])) { frac = frac * 10.0 + static_cast<f64>(s[i] - u8'0'); scale *= 10.0; ++i; any = true; }
        }
        if (!any) return {};
        return static_cast<f32>(sign * (whole + frac / scale));
    }

    [[nodiscard]] inline Optional<bool> ParseBool(core::StringView value)
    {
        const core::StringView s = Trim(value);
        if (s == core::StringView(u8"true") || s == core::StringView(u8"1") || s == core::StringView(u8"yes")) return true;
        if (s == core::StringView(u8"false") || s == core::StringView(u8"0") || s == core::StringView(u8"no")) return false;
        return {};
    }

    [[nodiscard]] inline Optional<Color> ParseColor(core::StringView value)
    {
        const core::StringView s = Trim(value);
        if (s.Size() == 0) return {};

        // #hex
        if (s[0] == u8'#')
        {
            const core::StringView hex = s.SubStr(1, s.Size() - 1);
            const usize len = hex.Size();
            auto nib = [&](usize k) { return HexValue(hex[k]); };
            f32 r = 0, g = 0, b = 0, a = 1.0f;
            if (len == 3)
            {
                const i32 hr = nib(0), hg = nib(1), hb = nib(2);
                if (hr < 0 || hg < 0 || hb < 0) return {};
                r = (hr * 17) / 255.0f; g = (hg * 17) / 255.0f; b = (hb * 17) / 255.0f;
            }
            else if (len == 6 || len == 8)
            {
                for (usize k = 0; k < len; ++k) if (nib(k) < 0) return {};
                r = (nib(0) * 16 + nib(1)) / 255.0f;
                g = (nib(2) * 16 + nib(3)) / 255.0f;
                b = (nib(4) * 16 + nib(5)) / 255.0f;
                if (len == 8) a = (nib(6) * 16 + nib(7)) / 255.0f;
            }
            else { return {}; }
            return Color{ r, g, b, a };
        }

        // rgb() / rgba()
        const bool rgba = (s.Size() > 5 && s.SubStr(0, 5) == core::StringView(u8"rgba("));
        const bool rgb = (s.Size() > 4 && s.SubStr(0, 4) == core::StringView(u8"rgb("));
        if (rgb || rgba)
        {
            const usize open = rgba ? 5 : 4;
            usize close = open;
            while (close < s.Size() && s[close] != u8')') ++close;
            const core::StringView inside = s.SubStr(open, close - open);
            f32 comps[4] = { 0, 0, 0, 1.0f };
            usize ci = 0, start = 0;
            for (usize i = 0; i <= inside.Size() && ci < 4; ++i)
            {
                if (i == inside.Size() || inside[i] == u8',')
                {
                    const Optional<f32> v = ParseLength(inside.SubStr(start, i - start));
                    if (!v.HasValue()) return {};
                    comps[ci] = ci < 3 ? (v.Value() / 255.0f) : v.Value(); // alpha is 0..1
                    ++ci;
                    start = i + 1;
                }
            }
            return Color{ comps[0], comps[1], comps[2], comps[3] };
        }

        // named
        return ParseNamedColor(s);
    }

    // 1-4 whitespace-separated lengths in CSS shorthand order (top / right / bottom / left).
    [[nodiscard]] inline Optional<Thickness> ParseThickness(core::StringView value)
    {
        f32 parts[4];
        usize count = 0, start = 0;
        const core::StringView s = Trim(value);
        for (usize i = 0; i <= s.Size(); ++i)
        {
            const bool boundary = (i == s.Size()) || IsWhiteSpace(s[i]);
            if (boundary)
            {
                if (i > start)
                {
                    if (count == 4) return {}; // too many
                    const Optional<f32> v = ParseLength(s.SubStr(start, i - start));
                    if (!v.HasValue()) return {};
                    parts[count++] = v.Value();
                }
                start = i + 1;
            }
        }
        if (count == 0) return {};

        f32 top, right, bottom, left;
        if (count == 1) { top = right = bottom = left = parts[0]; }
        else if (count == 2) { top = bottom = parts[0]; right = left = parts[1]; }
        else if (count == 3) { top = parts[0]; right = left = parts[1]; bottom = parts[2]; }
        else { top = parts[0]; right = parts[1]; bottom = parts[2]; left = parts[3]; }
        return Thickness{ left, top, right, bottom };
    }

    // border-radius: 1-4 lengths in CSS corner order (top-left, top-right, bottom-right,
    // bottom-left; 1 = all, 2 = TL/BR + TR/BL, 3 = TL, TR/BL, BR).
    [[nodiscard]] inline Optional<vg::CornerRadii> ParseCornerRadii(core::StringView value)
    {
        f32 parts[4];
        usize count = 0, start = 0;
        const core::StringView s = Trim(value);
        for (usize i = 0; i <= s.Size(); ++i)
        {
            const bool boundary = (i == s.Size()) || IsWhiteSpace(s[i]);
            if (boundary)
            {
                if (i > start)
                {
                    if (count == 4) return {};
                    const Optional<f32> v = ParseLength(s.SubStr(start, i - start));
                    if (!v.HasValue()) return {};
                    parts[count++] = v.Value();
                }
                start = i + 1;
            }
        }
        if (count == 0) return {};

        f32 tl, tr, br, bl;
        if (count == 1) { tl = tr = br = bl = parts[0]; }
        else if (count == 2) { tl = br = parts[0]; tr = bl = parts[1]; }
        else if (count == 3) { tl = parts[0]; tr = bl = parts[1]; br = parts[2]; }
        else { tl = parts[0]; tr = parts[1]; br = parts[2]; bl = parts[3]; }
        return vg::CornerRadii{ tl, tr, br, bl };
    }

    // The `border` shorthand: any order of a width (a length), a style keyword (ignored - only
    // solid is drawn), and a color. Missing width defaults to 1; missing color to black. A
    // leading "none"/"0" yields width 0. Returns width + color.
    struct BorderShorthand { f32 Width = 1.0f; Color LineColor{ 0.0f, 0.0f, 0.0f, 1.0f }; };
    [[nodiscard]] inline Optional<BorderShorthand> ParseBorder(core::StringView value)
    {
        BorderShorthand out;
        bool any = false;
        const core::StringView s = Trim(value);
        usize start = 0;
        for (usize i = 0; i <= s.Size(); ++i)
        {
            const bool boundary = (i == s.Size()) || IsWhiteSpace(s[i]);
            if (!boundary) continue;
            if (i > start)
            {
                const core::StringView tok = s.SubStr(start, i - start);
                if (tok == core::StringView(u8"none")) { out.Width = 0.0f; any = true; }
                else if (tok == core::StringView(u8"solid") || tok == core::StringView(u8"dashed")
                      || tok == core::StringView(u8"dotted") || tok == core::StringView(u8"hidden")) { any = true; }
                else if (Optional<Color> c = ParseColor(tok); c.HasValue()) { out.LineColor = c.Value(); any = true; }
                else if (Optional<f32> w = ParseLength(tok); w.HasValue()) { out.Width = w.Value(); any = true; }
            }
            start = i + 1;
        }
        if (!any) return {};
        return out;
    }

    // A small named-color set (core has only White/Black/Red/Green/Blue/Transparent).
    [[nodiscard]] inline Optional<Color> ParseNamedColor(core::StringView s)
    {
        if (s == core::StringView(u8"white"))       return Color::White;
        if (s == core::StringView(u8"black"))       return Color::Black;
        if (s == core::StringView(u8"red"))         return Color::Red;
        if (s == core::StringView(u8"green"))       return Color::Green;
        if (s == core::StringView(u8"blue"))        return Color::Blue;
        if (s == core::StringView(u8"transparent")) return Color::Transparent;
        if (s == core::StringView(u8"yellow"))      return Color{ 1.0f, 1.0f, 0.0f, 1.0f };
        if (s == core::StringView(u8"cyan"))        return Color{ 0.0f, 1.0f, 1.0f, 1.0f };
        if (s == core::StringView(u8"magenta"))     return Color{ 1.0f, 0.0f, 1.0f, 1.0f };
        if (s == core::StringView(u8"gray") || s == core::StringView(u8"grey")) return Color{ 0.5f, 0.5f, 0.5f, 1.0f };
        if (s == core::StringView(u8"orange"))      return Color{ 1.0f, 0.647f, 0.0f, 1.0f };
        return {};
    }
}
