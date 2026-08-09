# Draconic::Script.Wren.Pipeline - `draconic.script.wren.pipeline` (the Wren cook service).
# The Wren-specific cook (compile-check + `static properties` Fiber probe + starter) that the
# neutral ScriptClassAssetBuilder resolves by language. Cook-only (UI-free) so Tools.Cook /
# Tools.Export can link it; the editor-UI half lives in Editor/Editor.Script.Wren.
add_library(Draconic.Script.Wren.Pipeline STATIC)
add_library(Draconic::Script.Wren.Pipeline ALIAS Draconic.Script.Wren.Pipeline)
target_link_libraries(Draconic.Script.Wren.Pipeline PUBLIC
    Draconic::Core Draconic::Pipeline.Core Draconic::Content
    Draconic::Script Draconic::Script.Resource Draconic::Script.Facades
    Draconic::Script.Pipeline Draconic::Script.Wren Draconic::Policy)
target_sources(Draconic.Script.Wren.Pipeline
    PRIVATE
        WrenScriptCookImpl.cpp
    PUBLIC
        FILE_SET script_wren_pipeline_modules TYPE CXX_MODULES FILES WrenScriptCook.cppm)
