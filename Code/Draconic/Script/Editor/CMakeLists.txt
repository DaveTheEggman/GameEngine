# Draconic::ScriptEditor - `draconic.script.editor` (script class asset/builder/importer, tooling).
# Backend-neutral by construction (B3): links Draconic::Script for the REGISTRY only -
# never a concrete backend library (entry points register backends).
add_library(DraconicScriptEditor STATIC)
add_library(Draconic::ScriptEditor ALIAS DraconicScriptEditor)
target_link_libraries(DraconicScriptEditor PUBLIC
    Draconic::Core Draconic::Editor Draconic::EditorCore Draconic::Content
    Draconic::Script Draconic::ScriptResource Draconic::Policy)
target_sources(DraconicScriptEditor PUBLIC FILE_SET script_editor_modules TYPE CXX_MODULES FILES ScriptAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
