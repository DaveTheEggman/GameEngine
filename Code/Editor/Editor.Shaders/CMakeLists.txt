# Editor::Shaders - `editor.shaders`: the custom shader's editor side (the HLSL lexer for
# CodeEditView, the shader page). Editor exe only; the cook (Pipeline::Shaders) stays UI-free.
util_add_engine_library(Editor.Shaders ALIAS Editor::Shaders)
target_link_libraries(Editor.Shaders PUBLIC
    Foundation::Core Foundation::Content Foundation::Graphics Foundation::Runtime
    Foundation::Runtime.Client Foundation::Scene Foundation::Geometry Foundation::Shaders
    Foundation::Shaders.System Foundation::Shaders.Resource Foundation::Materials
    Foundation::Materials.Resource Foundation::UI Foundation::UI.Toolkit Engine::Render
    Pipeline::Shaders Pipeline::Materials Editor::Core Editor::App Editor::Preview
    Foundation::Policy)
target_sources(Editor.Shaders
    PRIVATE
        HlslLexerImpl.cpp
        ShaderPageImpl.cpp
    PUBLIC
        FILE_SET editor_shaders_modules TYPE CXX_MODULES FILES ShaderEditor.cppm ShaderPage.cppm)
