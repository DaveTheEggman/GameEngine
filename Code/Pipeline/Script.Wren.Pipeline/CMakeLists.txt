# Pipeline::Script.Wren - `script.wren.pipeline` (the Wren cook service).
# The Wren-specific cook (compile-check + `static properties` Fiber probe + starter) that the
# neutral ScriptClassAssetBuilder resolves by language. Cook-only (UI-free) so Tools.Cook /
# Tools.Export can link it; the editor-UI half lives in Editor/Editor.Script.Wren.
add_library(Script.Wren.Pipeline STATIC)
add_library(Pipeline::Script.Wren ALIAS Script.Wren.Pipeline)
target_link_libraries(Script.Wren.Pipeline PUBLIC
    Foundation::Core Pipeline::Core Foundation::Content
    Foundation::Script Foundation::Script.Resource Foundation::Script.Facades
    Pipeline::Script Foundation::Script.Wren Foundation::Policy)
target_sources(Script.Wren.Pipeline
    PRIVATE
        WrenScriptCookImpl.cpp
    PUBLIC
        FILE_SET script_wren_pipeline_modules TYPE CXX_MODULES FILES WrenScriptCook.cppm)
