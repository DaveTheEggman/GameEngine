# Pipeline::Script - `draconic.script.editor` (script class asset/builder/importer, tooling).
# Backend-neutral by construction (B3): links Foundation::Script for the REGISTRY only -
# never a concrete backend library (entry points register backends).
add_library(Script.Pipeline STATIC)
add_library(Pipeline::Script ALIAS Script.Pipeline)
target_link_libraries(Script.Pipeline PUBLIC
    Foundation::Core Pipeline::Core Editor::Core Foundation::Content
    Foundation::Script Foundation::Script.Resource Foundation::Policy)
target_sources(Script.Pipeline
    PRIVATE
        ScriptAssetImpl.cpp
    PUBLIC FILE_SET script_editor_modules TYPE CXX_MODULES FILES ScriptAsset.cppm)

