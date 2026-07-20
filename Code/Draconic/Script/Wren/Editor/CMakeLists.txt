# Draconic::ScriptWrenEditor - `draconic.script.wren.editor` (the Wren cook service).
# The Wren-specific cook (compile-check + `static properties` Fiber probe + starter) that
# the neutral ScriptClassAssetBuilder resolves by language. Depends on the Wren backend
# (cook VM) + the neutral editor interface + facades (the cook VM's "main" surface).
add_library(DraconicScriptWrenEditor STATIC)
add_library(Draconic::ScriptWrenEditor ALIAS DraconicScriptWrenEditor)
target_link_libraries(DraconicScriptWrenEditor PUBLIC
    Draconic::Core Draconic::Editor Draconic::EditorCore Draconic::Content
    Draconic::Script Draconic::ScriptResource Draconic::ScriptFacades
    Draconic::ScriptEditor Draconic::ScriptWren Draconic::Policy)
target_sources(DraconicScriptWrenEditor
    PRIVATE
        WrenScriptCookImpl.cpp
    PUBLIC
        FILE_SET script_wren_editor_modules TYPE CXX_MODULES FILES WrenScriptCook.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
