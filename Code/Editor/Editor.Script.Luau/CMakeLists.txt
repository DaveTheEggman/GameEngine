# Editor::Script.Luau - `editor.script.luau`: the EDITOR-UI services (CodeEditView
# lexer tables). Only the editor app links this; kept out of the Pipeline cook collection so
# Tools.Cook / Tools.Export stay UI-free.
add_library(Editor.Script.Luau STATIC)
add_library(Editor::Script.Luau ALIAS Editor.Script.Luau)
target_link_libraries(Editor.Script.Luau PUBLIC
    Foundation::Core Foundation::UI.Toolkit Foundation::Policy)
target_sources(Editor.Script.Luau
    PRIVATE
        LuauEditorUIImpl.cpp
    PUBLIC
        FILE_SET editor_script_luau_modules TYPE CXX_MODULES FILES LuauEditorUI.cppm)
