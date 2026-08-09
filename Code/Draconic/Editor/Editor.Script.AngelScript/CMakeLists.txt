# Draconic::Editor.Script.AngelScript - `draconic.editor.script.angelscript`: the EDITOR-UI
# services (CodeEditView lexer tables). Only the editor app links this; kept out of the Pipeline
# cook collection so Tools.Cook / Tools.Export stay UI-free.
add_library(Draconic.Editor.Script.AngelScript STATIC)
add_library(Draconic::Editor.Script.AngelScript ALIAS Draconic.Editor.Script.AngelScript)
target_link_libraries(Draconic.Editor.Script.AngelScript PUBLIC
    Draconic::Core Draconic::UI.Toolkit Draconic::Policy)
target_sources(Draconic.Editor.Script.AngelScript
    PRIVATE
        AngelScriptEditorUIImpl.cpp
    PUBLIC
        FILE_SET editor_script_angelscript_modules TYPE CXX_MODULES FILES AngelScriptEditorUI.cppm)
