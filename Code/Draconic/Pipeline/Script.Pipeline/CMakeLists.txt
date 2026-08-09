# Draconic::Script.Pipeline - `draconic.script.editor` (script class asset/builder/importer, tooling).
# Backend-neutral by construction (B3): links Draconic::Script for the REGISTRY only -
# never a concrete backend library (entry points register backends).
add_library(Draconic.Script.Pipeline STATIC)
add_library(Draconic::Script.Pipeline ALIAS Draconic.Script.Pipeline)
target_link_libraries(Draconic.Script.Pipeline PUBLIC
    Draconic::Core Draconic::Pipeline.Core Draconic::Editor.Core Draconic::Content
    Draconic::Script Draconic::Script.Resource Draconic::Policy)
target_sources(Draconic.Script.Pipeline
    PRIVATE
        ScriptAssetImpl.cpp
    PUBLIC FILE_SET script_editor_modules TYPE CXX_MODULES FILES ScriptAsset.cppm)

