// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Script.Luau - the `editor.script.luau` module.
//
// Luau-specific EDITOR-UI services: everything the in-editor experience needs that depends on
// ui.toolkit and therefore cannot live in the cook target (the cook links into Tools.Cook/
// Tools.Export, which must stay UI-free). Today that is the Luau syntax tables for CodeEditView
// highlighting (over the toolkit's LuaLikeLexer), registered into the CodeLexerRegistry by
// language id.

module;
#include "Core/Prelude.h"

export module editor.script.luau;

import foundation.core;

export namespace editor
{
    /// Registers Luau's editor-UI services (the CodeEditView lexer, under both the canonical
    /// "luau" id and the "lua" alias). An editor entry point's job, beside
    /// RegisterLuauScriptBackend/RegisterLuauScriptCook.
    void RegisterLuauEditorUI();
}
