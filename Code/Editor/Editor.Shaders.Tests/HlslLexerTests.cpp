// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The HLSL lexer: registered under "hlsl" and "hlsli"; types, keywords, numbers and comments
// classified; a '#' line is one preprocessor token; the properties block's types read as types.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import foundation.core;
import foundation.ui.toolkit;
import editor.shaders;

using namespace foundation::core;
using namespace foundation::ui::toolkit;

namespace
{
    // The kinds of the tokens on one line, in order.
    Array<CodeTokenKind> Kinds(ICodeLexer& lexer, StringView line)
    {
        Array<CodeToken> tokens;
        (void)lexer.LexLine(line, 0, tokens);
        Array<CodeTokenKind> kinds;
        for (const CodeToken& t : tokens)
        {
            kinds.PushBack(t.kind);
        }
        return kinds;
    }

    bool HasKind(const Array<CodeTokenKind>& kinds, CodeTokenKind kind)
    {
        for (const CodeTokenKind k : kinds)
        {
            if (k == kind)
            {
                return true;
            }
        }
        return false;
    }
}

TEST_CASE("hlsl lexer: registered under hlsl and hlsli, case-insensitively")
{
    editor::RegisterHlslEditorUI(DefaultAllocator());
    CHECK(CodeLexerRegistry::Get().Create(u8"hlsl").Get() != nullptr);
    CHECK(CodeLexerRegistry::Get().Create(u8"HLSLI").Get() != nullptr);
}

TEST_CASE("hlsl lexer: types, keywords, numbers, comments and preprocessor lines")
{
    editor::RegisterHlslEditorUI(DefaultAllocator());
    UniquePtr<ICodeLexer> lexer = CodeLexerRegistry::Get().Create(u8"hlsl");
    REQUIRE(lexer.Get() != nullptr);

    const Array<CodeTokenKind> decl = Kinds(*lexer, u8"float4 PSMain(VSOutput input) : SV_Target0 {");
    REQUIRE(decl.Size() >= 2u);
    CHECK(decl[0] == CodeTokenKind::Type);    // float4
    CHECK(decl[1] == CodeTokenKind::Default); // PSMain

    const Array<CodeTokenKind> body = Kinds(*lexer, u8"if (c.a < 0.5) discard; // cut out");
    CHECK(body[0] == CodeTokenKind::Keyword); // if
    CHECK(HasKind(body, CodeTokenKind::Number));
    CHECK(body[body.Size() - 1] == CodeTokenKind::Comment);

    // A '#' line is one token, even indented.
    const Array<CodeTokenKind> include = Kinds(*lexer, u8"  #include \"lighting.hlsli\"");
    REQUIRE(include.Size() == 1u);
    CHECK(include[0] == CodeTokenKind::Preprocessor);

    // Resources and the properties block's own types are types; `properties` is a keyword.
    CHECK(Kinds(*lexer, u8"Texture2D")[0] == CodeTokenKind::Type);
    CHECK(Kinds(*lexer, u8"Color")[0] == CodeTokenKind::Type);
    CHECK(Kinds(*lexer, u8"properties")[0] == CodeTokenKind::Keyword);

    // Block comments do not nest: the first terminator closes, and the state carries over lines.
    Array<CodeToken> tokens;
    CHECK(lexer->LexLine(u8"/* a /* b */ done", 0, tokens) == 0);
    tokens.Clear();
    CHECK(lexer->LexLine(u8"/* open", 0, tokens) != 0);
}
