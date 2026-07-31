# Draconic::EditorScript - `draconic.editor.script`: the ScriptPage (behavior source text
# editor + save->recook->hot-reload + inline compile errors). Editor exe only.
add_library(Draconic.Editor.Script STATIC)
add_library(Draconic::EditorScript ALIAS Draconic.Editor.Script)
target_link_libraries(Draconic.Editor.Script PUBLIC
    Draconic::Core Draconic::Content Draconic::RuntimeClient Draconic::UI Draconic::UIToolkit
    Draconic::Script Draconic::ScriptFacades Draconic::ScriptEditor Draconic::ScriptResource
    Draconic::EditorCore Draconic::EditorApp Draconic::Policy)
target_sources(Draconic.Editor.Script PRIVATE ScriptPageImpl.cpp ScriptCompletionImpl.cpp ScriptApiSurfaceImpl.cpp ScriptApiBrowserImpl.cpp PUBLIC FILE_SET editor_script_modules TYPE CXX_MODULES FILES ScriptPage.cppm)

# The headless editing seam (ScriptSourceDocument, in draconic.script.editor) is covered by the
# script pipeline tests; the UI page itself is exercised by the editor smoke.

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Editor.Script.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
