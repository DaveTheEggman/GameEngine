# Draconic::Script.AngelScript.Pipeline - `draconic.script.angelscript.pipeline` (the AngelScript
# cook service): compile-check + shared handler scan + [metadata] PROPERTY HARVEST + starter.
# Cook-only; the SDK (Draconic::AngelScript) is a PRIVATE dep (impl unit #includes angelscript.h
# + scriptbuilder for metadata harvest - GCC module hygiene). The editor-UI half lives in
# Editor/Editor.Script.AngelScript.
add_library(Draconic.Script.AngelScript.Pipeline STATIC)
add_library(Draconic::Script.AngelScript.Pipeline ALIAS Draconic.Script.AngelScript.Pipeline)
target_link_libraries(Draconic.Script.AngelScript.Pipeline
    PUBLIC
        Draconic::Core Draconic::Pipeline.Core Draconic::Content
        Draconic::Script Draconic::Script.Resource Draconic::Script.Facades
        Draconic::Script.Pipeline Draconic::Script.AngelScript Draconic::Policy
    PRIVATE
        Draconic::AngelScript)
target_sources(Draconic.Script.AngelScript.Pipeline
    PRIVATE
        AngelScriptScriptCookImpl.cpp
    PUBLIC
        FILE_SET script_as_pipeline_modules TYPE CXX_MODULES FILES AngelScriptScriptCook.cppm)
