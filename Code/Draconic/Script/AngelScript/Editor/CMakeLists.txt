# Draconic::ScriptAngelScriptEditor - `draconic.script.angelscript.editor` (the AngelScript
# cook service): compile-check + shared handler scan + [metadata] PROPERTY HARVEST + starter.
# Depends on the AngelScript backend (cook VM + engine-handle seam) + the neutral editor
# interface + facades (the cook VM's "main" surface). The SDK (Draconic::AngelScript) is a
# PRIVATE dep: the cook's implementation unit #includes <angelscript.h> and the vendored
# <scriptbuilder/scriptbuilder.h> to drive metadata harvest (GCC module hygiene - impl only).
add_library(DraconicScriptAngelScriptEditor STATIC)
add_library(Draconic::ScriptAngelScriptEditor ALIAS DraconicScriptAngelScriptEditor)
target_link_libraries(DraconicScriptAngelScriptEditor
    PUBLIC
        Draconic::Core Draconic::Editor Draconic::EditorCore Draconic::Content
        Draconic::Script Draconic::ScriptResource Draconic::ScriptFacades
        Draconic::ScriptEditor Draconic::ScriptAngelScript Draconic::Policy
    PRIVATE
        Draconic::AngelScript)
target_sources(DraconicScriptAngelScriptEditor
    PRIVATE
        AngelScriptScriptCookImpl.cpp
    PUBLIC
        FILE_SET script_as_editor_modules TYPE CXX_MODULES FILES AngelScriptScriptCook.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
