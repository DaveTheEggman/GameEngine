# Draconic::ScriptWrenEditor - `draconic.script.wren.editor` (the Wren cook service).
# The Wren-specific cook (compile-check + `static properties` Fiber probe + starter) that
# the neutral ScriptClassAssetBuilder resolves by language. Depends on the Wren backend
# (cook VM) + the neutral editor interface + facades (the cook VM's "main" surface).
add_library(Draconic.Script.Wren.Editor STATIC)
add_library(Draconic::ScriptWrenEditor ALIAS Draconic.Script.Wren.Editor)
target_link_libraries(Draconic.Script.Wren.Editor PUBLIC
    Draconic::Core Draconic::Editor Draconic::EditorCore Draconic::Content
    Draconic::Script Draconic::ScriptResource Draconic::ScriptFacades
    Draconic::ScriptEditor Draconic::ScriptWren Draconic::Policy)
target_sources(Draconic.Script.Wren.Editor
    PRIVATE
        WrenScriptCookImpl.cpp
    PUBLIC
        FILE_SET script_wren_editor_modules TYPE CXX_MODULES FILES WrenScriptCook.cppm)

# Draconic::ScriptWrenEditorUI - `draconic.script.wren.editor.ui`: Wren's EDITOR-UI services
# (CodeEditView lexer tables; the introspection completion provider joins later). Separate
# from the cook target above because that one links into RaptorCook/RaptorExport, which must
# stay UI-free; only the editor app links this.
add_library(Draconic.Script.Wren.Editor.UI STATIC)
add_library(Draconic::ScriptWrenEditorUI ALIAS Draconic.Script.Wren.Editor.UI)
target_link_libraries(Draconic.Script.Wren.Editor.UI PUBLIC
    Draconic::Core Draconic::UIToolkit Draconic::Policy)
target_sources(Draconic.Script.Wren.Editor.UI
    PRIVATE
        WrenEditorUIImpl.cpp
    PUBLIC
        FILE_SET script_wren_editor_ui_modules TYPE CXX_MODULES FILES WrenEditorUI.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Script.Wren.Editor.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
