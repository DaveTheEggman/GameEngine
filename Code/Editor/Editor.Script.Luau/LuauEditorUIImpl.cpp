// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Script.Luau - the `editor.script.luau` module.
//
// The Luau syntax tables + lexer registration. Luau lexes on the toolkit's LuaLikeLexer: `--`
// line comments, `--[[ ]]` / `--[=[ ]=]` long comments, `[[ ]]` long strings, and backtick
// interpolated strings. Comment-toggle (Ctrl+/) uses `--`.

module;
#include "Core/Prelude.h"

module editor.script.luau;

import foundation.core;
import foundation.ui.toolkit;

using namespace foundation::core;

namespace editor
{
    namespace
    {
        namespace toolkit = foundation::ui::toolkit;

        // Luau reserved words + the contextual keywords (continue / export / type) it adds over
        // Lua. Value literals nil/true/false read as keywords.
        constexpr StringView kLuauKeywords[] = {
            u8"and",    u8"break", u8"continue", u8"do",     u8"else",   u8"elseif",
            u8"end",    u8"export", u8"false",   u8"for",    u8"function", u8"if",
            u8"in",     u8"local", u8"nil",      u8"not",    u8"or",     u8"repeat",
            u8"return", u8"then",  u8"true",     u8"type",   u8"until",  u8"while"};
        // Luau's built-in type-annotation names.
        constexpr StringView kLuauTypes[] = {u8"any",    u8"boolean", u8"buffer", u8"never",
                                             u8"number", u8"string",  u8"thread", u8"unknown"};

        UniquePtr<toolkit::ICodeLexer> MakeLuauLexer()
        {
            toolkit::LuaLikeLexerSpec spec;
            spec.keywords = Span<const StringView>(kLuauKeywords, ArrayCount(kLuauKeywords));
            spec.types = Span<const StringView>(kLuauTypes, ArrayCount(kLuauTypes));
            return UniquePtr<toolkit::ICodeLexer>(
                foundation::core::DefaultAllocator().New<toolkit::LuaLikeLexer>(spec), foundation::core::DefaultAllocator());
        }
    }

    void RegisterLuauEditorUI()
    {
        auto& registry = toolkit::CodeLexerRegistry::Get();
        registry.Register(u8"luau", [] { return MakeLuauLexer(); });
        registry.Register(u8"lua", [] { return MakeLuauLexer(); });
    }
}
