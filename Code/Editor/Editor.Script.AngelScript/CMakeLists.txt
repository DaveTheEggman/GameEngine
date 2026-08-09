# Editor::Script.AngelScript - `editor.script.angelscript`: the EDITOR-UI
# services (CodeEditView lexer tables). Only the editor app links this; kept out of the Pipeline
# cook collection so Tools.Cook / Tools.Export stay UI-free.
add_library(Editor.Script.AngelScript STATIC)
add_library(Editor::Script.AngelScript ALIAS Editor.Script.AngelScript)
target_link_libraries(Editor.Script.AngelScript PUBLIC
    Foundation::Core Foundation::UI.Toolkit Foundation::Policy)
target_sources(Editor.Script.AngelScript
    PRIVATE
        AngelScriptEditorUIImpl.cpp
    PUBLIC
        FILE_SET editor_script_angelscript_modules TYPE CXX_MODULES FILES AngelScriptEditorUI.cppm)
