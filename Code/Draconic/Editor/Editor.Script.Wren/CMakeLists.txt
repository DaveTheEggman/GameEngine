# Editor::Script.Wren - `draconic.editor.script.wren`: Wren's EDITOR-UI services
# (CodeEditView lexer tables). Only the editor app links this; kept out of the Pipeline cook
# collection so Tools.Cook / Tools.Export stay UI-free.
add_library(Editor.Script.Wren STATIC)
add_library(Editor::Script.Wren ALIAS Editor.Script.Wren)
target_link_libraries(Editor.Script.Wren PUBLIC
    Foundation::Core Foundation::UI.Toolkit Foundation::Policy)
target_sources(Editor.Script.Wren
    PRIVATE
        WrenEditorUIImpl.cpp
    PUBLIC
        FILE_SET editor_script_wren_modules TYPE CXX_MODULES FILES WrenEditorUI.cppm)
