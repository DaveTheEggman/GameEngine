# Draconic::Editor.Script.Wren - `draconic.editor.script.wren`: Wren's EDITOR-UI services
# (CodeEditView lexer tables). Only the editor app links this; kept out of the Pipeline cook
# collection so Tools.Cook / Tools.Export stay UI-free.
add_library(Draconic.Editor.Script.Wren STATIC)
add_library(Draconic::Editor.Script.Wren ALIAS Draconic.Editor.Script.Wren)
target_link_libraries(Draconic.Editor.Script.Wren PUBLIC
    Draconic::Core Draconic::UI.Toolkit Draconic::Policy)
target_sources(Draconic.Editor.Script.Wren
    PRIVATE
        WrenEditorUIImpl.cpp
    PUBLIC
        FILE_SET editor_script_wren_modules TYPE CXX_MODULES FILES WrenEditorUI.cppm)
