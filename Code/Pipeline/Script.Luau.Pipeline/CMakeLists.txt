# Pipeline::Script.Luau - `script.luau.pipeline` (the Luau cook service).
# The Luau-specific cook (compile-check + construct-and-walk property harvest + starters) that
# the neutral ScriptClassAssetBuilder resolves by language. Cook-only (UI-free) so Tools.Cook /
# Tools.Export can link it; the editor-UI half rides the neutral ScriptPage.
add_library(Script.Luau.Pipeline STATIC)
add_library(Pipeline::Script.Luau ALIAS Script.Luau.Pipeline)
target_link_libraries(Script.Luau.Pipeline PUBLIC
    Foundation::Core Pipeline::Core Foundation::Content
    Foundation::Script Foundation::Script.Resource Foundation::Script.Facades
    Pipeline::Script Foundation::Script.Luau Foundation::Policy)
target_sources(Script.Luau.Pipeline
    PRIVATE
        LuauScriptCookImpl.cpp
    PUBLIC
        FILE_SET script_luau_pipeline_modules TYPE CXX_MODULES FILES LuauScriptCook.cppm)
