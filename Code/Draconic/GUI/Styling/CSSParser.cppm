// Draconic GUI - :css_parser partition
//
// CSSParser: turns `.css` text into a StyleSheet. Ported from eepp's css/StyleSheetParser
// (common subset). Handles `/* comments */`, comma-separated selector lists, and
// `name: value;` declaration blocks; best-effort (malformed bits are skipped). Deferred:
// @media / @keyframes / @import, custom properties (--var), nesting, !important.

module;
#include "Core/Prelude.h"

export module draconic.gui:css_parser;

import draconic.core;   // String, StringView, Move
import :style_rule;
import :style_sheet;
import :media_query;

using namespace draconic::core;
namespace core = draconic::core;

namespace draconic::gui
{
    // Remove /* ... */ comments (run-copied so indices in the result are contiguous).
    [[nodiscard]] inline core::String StripComments(core::StringView s)
    {
        core::String out;
        const usize n = s.Size();
        usize runStart = 0, i = 0;
        while (i < n)
        {
            if (i + 1 < n && s[i] == u8'/' && s[i + 1] == u8'*')
            {
                out.Append(s.SubStr(runStart, i - runStart));
                i += 2;
                while (i + 1 < n && !(s[i] == u8'*' && s[i + 1] == u8'/')) ++i;
                i = (i + 1 < n) ? i + 2 : n; // skip closing */ (or to end)
                runStart = i;
            }
            else { ++i; }
        }
        out.Append(s.SubStr(runStart, n - runStart));
        return out;
    }
}

export namespace draconic::gui
{
    class CSSParser
    {
    public:
        // Parse CSS source into a StyleSheet.
        [[nodiscard]] static StyleSheet Parse(core::StringView css)
        {
            const core::String cleaned = StripComments(css);
            StyleSheet sheet;
            ParseBlock(sheet, cleaned.AsView(), MediaQuery{});
            return sheet;
        }

    private:
        // Parse a run of rules (and nested @media blocks), attaching `media` to each rule.
        static void ParseBlock(StyleSheet& sheet, core::StringView src, const MediaQuery& media)
        {
            const usize n = src.Size();
            usize i = 0;
            while (i < n)
            {
                while (i < n && IsWhiteSpace(src[i])) ++i;
                if (i >= n) break;

                if (src[i] == u8'@')
                {
                    // At-rule: read the prelude up to '{' (block) or ';' (statement).
                    const usize atStart = i;
                    while (i < n && src[i] != u8'{' && src[i] != u8';') ++i;
                    const core::StringView prelude = Trim(src.SubStr(atStart, i - atStart));
                    if (i < n && src[i] == u8';') { ++i; continue; } // at-statement (e.g. @import) - skipped
                    if (i >= n) break;

                    // Brace-matched block for this at-rule.
                    ++i; // consume '{'
                    const usize blockStart = i;
                    i32 depth = 1;
                    while (i < n && depth > 0)
                    {
                        if (src[i] == u8'{') ++depth;
                        else if (src[i] == u8'}') { --depth; if (depth == 0) break; }
                        ++i;
                    }
                    const core::StringView inner = src.SubStr(blockStart, i - blockStart);
                    if (i < n) ++i; // consume '}'

                    if (IsMedia(prelude))
                    {
                        const core::StringView condition = Trim(prelude.SubStr(6, prelude.Size() - 6));
                        ParseBlock(sheet, inner, MediaQuery{ condition }); // v1: inner media replaces outer
                    }
                    // unknown at-rule blocks are consumed and skipped
                    continue;
                }

                // Normal rule: selector list up to '{'.
                const usize selStart = i;
                while (i < n && src[i] != u8'{' && src[i] != u8'}') ++i;
                if (i >= n || src[i] != u8'{') break;
                const core::StringView selectorList = src.SubStr(selStart, i - selStart);
                ++i; // consume '{'

                const usize blockStart = i;
                while (i < n && src[i] != u8'}') ++i;
                const core::StringView block = src.SubStr(blockStart, i - blockStart);
                if (i < n) ++i; // consume '}'

                EmitRules(sheet, selectorList, block, media);
            }
        }

        [[nodiscard]] static bool IsMedia(core::StringView prelude) noexcept
        {
            return prelude.Size() >= 6 && prelude.SubStr(0, 6) == core::StringView(u8"@media");
        }

        // One rule per comma-separated selector, each carrying the block's declarations + media.
        static void EmitRules(StyleSheet& sheet, core::StringView selectorList, core::StringView block,
                              const MediaQuery& media)
        {
            usize start = 0;
            const usize n = selectorList.Size();
            for (usize i = 0; i <= n; ++i)
            {
                if (i == n || selectorList[i] == u8',')
                {
                    const core::StringView selector = Trim(selectorList.SubStr(start, i - start));
                    if (selector.Size() != 0)
                    {
                        StyleRule rule{ selector };
                        ApplyDeclarations(rule, block);
                        if (!media.IsEmpty()) rule.SetMedia(media);
                        sheet.AddRule(core::Move(rule));
                    }
                    start = i + 1;
                }
            }
        }

        // Split `name: value;` declarations into the rule.
        static void ApplyDeclarations(StyleRule& rule, core::StringView block)
        {
            usize start = 0;
            const usize n = block.Size();
            for (usize i = 0; i <= n; ++i)
            {
                if (i == n || block[i] == u8';')
                {
                    const core::StringView decl = block.SubStr(start, i - start);
                    const usize colon = FindChar(decl, u8':');
                    if (colon != kNotFound)
                    {
                        const core::StringView name = Trim(decl.SubStr(0, colon));
                        core::StringView value = Trim(decl.SubStr(colon + 1, decl.Size() - colon - 1));

                        // Trailing !important.
                        bool important = false;
                        const core::StringView flag(u8"!important");
                        if (value.Size() >= flag.Size()
                            && value.SubStr(value.Size() - flag.Size(), flag.Size()) == flag)
                        {
                            important = true;
                            value = Trim(value.SubStr(0, value.Size() - flag.Size()));
                        }

                        if (name.Size() != 0) rule.SetProperty(name, value, important);
                    }
                    start = i + 1;
                }
            }
        }

        static constexpr usize kNotFound = static_cast<usize>(-1);
        [[nodiscard]] static usize FindChar(core::StringView s, char8_t c) noexcept
        {
            for (usize i = 0; i < s.Size(); ++i)
                if (s[i] == c) return i;
            return kNotFound;
        }
    };
}
