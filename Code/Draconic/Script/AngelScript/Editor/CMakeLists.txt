# Draconic::ScriptAngelScriptEditor - `draconic.script.angelscript.editor` (the AngelScript
# cook service). A minimal cook: compile-check + shared handler scan + starter (property
# harvest deferred). Depends on the AngelScript backend (cook VM) + the neutral editor
# interface + facades (the cook VM's "main" surface).
add_library(DraconicScriptAngelScriptEditor STATIC)
add_library(Draconic::ScriptAngelScriptEditor ALIAS DraconicScriptAngelScriptEditor)
target_link_libraries(DraconicScriptAngelScriptEditor PUBLIC
    Draconic::Core Draconic::Editor Draconic::EditorCore Draconic::Content
    Draconic::Script Draconic::ScriptResource Draconic::ScriptFacades
    Draconic::ScriptEditor Draconic::ScriptAngelScript Draconic::Policy)
target_sources(DraconicScriptAngelScriptEditor
    PRIVATE
        AngelScriptScriptCookImpl.cpp
    PUBLIC
        FILE_SET script_as_editor_modules TYPE CXX_MODULES FILES AngelScriptScriptCook.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
