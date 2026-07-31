# Draconic::ScriptAngelScriptEditor - `draconic.script.angelscript.editor` (the AngelScript
# cook service): compile-check + shared handler scan + [metadata] PROPERTY HARVEST + starter.
# Depends on the AngelScript backend (cook VM + engine-handle seam) + the neutral editor
# interface + facades (the cook VM's "main" surface). The SDK (Draconic::AngelScript) is a
# PRIVATE dep: the cook's implementation unit #includes <angelscript.h> and the vendored
# <scriptbuilder/scriptbuilder.h> to drive metadata harvest (GCC module hygiene - impl only).
add_library(Draconic.Script.AngelScript.Editor STATIC)
add_library(Draconic::ScriptAngelScriptEditor ALIAS Draconic.Script.AngelScript.Editor)
target_link_libraries(Draconic.Script.AngelScript.Editor
    PUBLIC
        Draconic::Core Draconic::Editor Draconic::EditorCore Draconic::Content
        Draconic::Script Draconic::ScriptResource Draconic::ScriptFacades
        Draconic::ScriptEditor Draconic::ScriptAngelScript Draconic::Policy
    PRIVATE
        Draconic::AngelScript)
target_sources(Draconic.Script.AngelScript.Editor
    PRIVATE
        AngelScriptScriptCookImpl.cpp
    PUBLIC
        FILE_SET script_as_editor_modules TYPE CXX_MODULES FILES AngelScriptScriptCook.cppm)

# Draconic::ScriptAngelScriptEditorUI - `draconic.script.angelscript.editor.ui`: the
# EDITOR-UI services (CodeEditView lexer tables; the introspection completion provider joins
# later). Separate from the cook target above because that one links into RaptorCook/
# RaptorExport, which must stay UI-free; only the editor app links this.
add_library(Draconic.Script.AngelScript.Editor.UI STATIC)
add_library(Draconic::ScriptAngelScriptEditorUI ALIAS Draconic.Script.AngelScript.Editor.UI)
target_link_libraries(Draconic.Script.AngelScript.Editor.UI PUBLIC
    Draconic::Core Draconic::UIToolkit Draconic::Policy)
target_sources(Draconic.Script.AngelScript.Editor.UI
    PRIVATE
        AngelScriptEditorUIImpl.cpp
    PUBLIC
        FILE_SET script_as_editor_ui_modules TYPE CXX_MODULES FILES AngelScriptEditorUI.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Script.AngelScript.Editor.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
