// LuauEditorUI: registering the lexer makes it resolvable under both the canonical "luau" id
// and the "lua" alias, with Luau classification (`--` comments, long brackets, backtick strings).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import foundation.core;
import foundation.ui.toolkit;
import editor.script.luau;

using namespace foundation::core;
using namespace foundation::ui::toolkit;

namespace
{
    StringView TextOf(StringView line, const CodeToken& token)
    {
        return line.SubStr(token.byteBegin, token.byteEnd - token.byteBegin);
    }
    const CodeToken* Find(StringView line, const Array<CodeToken>& tokens, StringView text)
    {
        for (usize i = 0; i < tokens.Size(); ++i)
        {
            if (TextOf(line, tokens[i]) == text)
            {
                return &tokens[i];
            }
        }
        return nullptr;
    }
}

TEST_CASE("luau-editor-ui: LexerRegistration")
{
    editor::RegisterLuauEditorUI();
    CHECK(CodeLexerRegistry::Get().Create(u8"lua").Get() != nullptr); // the alias
    UniquePtr<ICodeLexer> lexer = CodeLexerRegistry::Get().Create(u8"luau");
    REQUIRE(lexer.Get() != nullptr);

    // Comment-toggle (Ctrl+/) uses Lua's `--`, not `//`.
    CHECK(lexer->LineCommentPrefix() == StringView(u8"--"));

    const StringView line = u8"local function step(dt) -- tick";
    Array<CodeToken> tokens;
    (void)lexer->LexLine(line, 0, tokens);
    REQUIRE(Find(line, tokens, u8"local") != nullptr);
    CHECK(Find(line, tokens, u8"local")->kind == CodeTokenKind::Keyword);
    CHECK(Find(line, tokens, u8"function")->kind == CodeTokenKind::Keyword);
    CHECK(Find(line, tokens, u8"step")->kind == CodeTokenKind::Default);
    CHECK(Find(line, tokens, u8"-- tick")->kind == CodeTokenKind::Comment);

    // A Luau type annotation classifies as Type.
    tokens.Clear();
    const StringView typed = u8"local n: number = 0";
    (void)lexer->LexLine(typed, 0, tokens);
    CHECK(Find(typed, tokens, u8"number")->kind == CodeTokenKind::Type);

    // A long comment `--[[ ... ]]` carries an open state across lines (0 = closed).
    tokens.Clear();
    const u32 state = lexer->LexLine(StringView(u8"--[[ open"), 0, tokens);
    CHECK(state != 0);
}
