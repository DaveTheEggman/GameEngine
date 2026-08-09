// Draconic::Editor.Script.AngelScript - the `draconic.editor.script.angelscript` module.
//
// AngelScript-specific EDITOR-UI services: everything the in-editor experience needs that
// depends on ui.toolkit and therefore cannot live in the cook target (the cook links into
// Draconic.Tools.Cook/Draconic.Tools.Export, which must stay UI-free). Today that is the AngelScript syntax
// tables for CodeEditView highlighting, registered into the toolkit's CodeLexerRegistry by
// language id; the rich completion provider (ICompletionProvider over the engine's
// AngelScript introspection seam) joins here with code-editor P4.

module;
#include "Core/Prelude.h"

export module draconic.editor.script.angelscript;

import draconic.core;

export namespace editor{
    /// Registers AngelScript's editor-UI services (the CodeEditView lexer, under both the
    /// canonical "angelscript" id and the "as" alias). An editor entry point's job, beside
    /// RegisterAngelScriptBackend/RegisterAngelScriptScriptCook.
    void RegisterAngelScriptEditorUI();
}
