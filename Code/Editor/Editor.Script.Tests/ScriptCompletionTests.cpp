// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// ScriptApiCompletionProvider over the SHARED ScriptApiSurface (built once through a throwaway
// backend manager - the runtime's registration sequence): type names at top level + a type's
// members after `Type.`. Proven identically on both backends (the surface resolves by language).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import foundation.core;
import foundation.ui.toolkit;
import foundation.script;
#ifdef OPTION_HAS_ANGELSCRIPT
import foundation.script.angelscript;
#endif
#ifdef OPTION_HAS_LUAU
import foundation.script.luau;
#endif
import editor.script;

using namespace foundation::core;
using namespace editor;
namespace toolkit = foundation::ui::toolkit;

namespace
{
    bool Contains(const Array<toolkit::CompletionCandidate>& out, const char8_t* label)
    {
        for (usize i = 0; i < out.Size(); ++i)
        {
            if (out[i].label.AsView() == StringView(label))
            {
                return true;
            }
        }
        return false;
    }

    // The completion checks, parameterized by the surface's language. The provider is backend-
    // neutral (resolves the API surface through the registry by language), so both backends see the
    // same reflected Core math surface.
    void CheckBoundApiCompletion(StringView language)
    {
        ScriptApiSurface surface;
        surface.SetLanguage(language);
        ScriptApiCompletionProvider provider;
        provider.SetSurface(&surface);

        toolkit::CodeDocument doc;
        Array<toolkit::CompletionCandidate> out;

        // Top level: bound type names (the reflected Core math surface is always registered).
        doc.SetText(u8"Flo");
        provider.Collect(doc, toolkit::CodePosition{0, 3}, StringView(u8"Flo"), out);
        CHECK(Contains(out, u8"Float3"));

        // Member context: `Float3.` offers that type's members (spelled the backend's way).
        doc.SetText(u8"Float3.");
        out.Clear();
        provider.Collect(doc, toolkit::CodePosition{0, 7}, StringView(u8""), out);
        REQUIRE(out.Size() > 0);
        CHECK(Contains(out, u8"Dot"));

        // Unknown receiver: nothing from this provider (the word provider still runs).
        doc.SetText(u8"nonsense.");
        out.Clear();
        provider.Collect(doc, toolkit::CodePosition{0, 9}, StringView(u8""), out);
        CHECK(out.Size() == 0);

        // Unknown language: silently empty.
        ScriptApiSurface unknownSurface;
        unknownSurface.SetLanguage(u8"cobol");
        ScriptApiCompletionProvider unknown;
        unknown.SetSurface(&unknownSurface);
        out.Clear();
        doc.SetText(u8"x");
        unknown.Collect(doc, toolkit::CodePosition{0, 1}, StringView(u8"x"), out);
        CHECK(out.Size() == 0);
    }
}

#ifdef OPTION_HAS_ANGELSCRIPT
TEST_CASE("editor-script: bound-API completion (angelscript)")
{
    foundation::script::angelscript::RegisterAngelScriptBackend();
    CheckBoundApiCompletion(u8"angelscript");
}
#endif // OPTION_HAS_ANGELSCRIPT

#ifdef OPTION_HAS_LUAU
TEST_CASE("editor-script: bound-API completion (luau)")
{
    foundation::script::RegisterLuauScriptBackend();
    CheckBoundApiCompletion(u8"luau");
}
#endif // OPTION_HAS_LUAU
