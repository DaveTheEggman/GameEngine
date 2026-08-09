# Pipeline::Script.AngelScript - `draconic.script.angelscript.pipeline` (the AngelScript
# cook service): compile-check + shared handler scan + [metadata] PROPERTY HARVEST + starter.
# Cook-only; the SDK (ThirdParty::AngelScript) is a PRIVATE dep (impl unit #includes angelscript.h
# + scriptbuilder for metadata harvest - GCC module hygiene). The editor-UI half lives in
# Editor/Editor.Script.AngelScript.
add_library(Script.AngelScript.Pipeline STATIC)
add_library(Pipeline::Script.AngelScript ALIAS Script.AngelScript.Pipeline)
target_link_libraries(Script.AngelScript.Pipeline
    PUBLIC
        Foundation::Core Pipeline::Core Foundation::Content
        Foundation::Script Foundation::Script.Resource Foundation::Script.Facades
        Pipeline::Script Foundation::Script.AngelScript Foundation::Policy
    PRIVATE
        ThirdParty::AngelScript)
target_sources(Script.AngelScript.Pipeline
    PRIVATE
        AngelScriptScriptCookImpl.cpp
    PUBLIC
        FILE_SET script_as_pipeline_modules TYPE CXX_MODULES FILES AngelScriptScriptCook.cppm)
