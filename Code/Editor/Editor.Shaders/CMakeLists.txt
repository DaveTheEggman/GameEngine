# Editor::Shaders - `editor.shaders`: the custom shader's editor side (the HLSL lexer for
# CodeEditView). Editor exe only; the cook (Pipeline::Shaders) stays UI-free.
util_add_engine_library(Editor.Shaders ALIAS Editor::Shaders)
target_link_libraries(Editor.Shaders PUBLIC
    Foundation::Core Foundation::UI.Toolkit Foundation::Policy)
target_sources(Editor.Shaders
    PRIVATE
        HlslLexerImpl.cpp
    PUBLIC
        FILE_SET editor_shaders_modules TYPE CXX_MODULES FILES ShaderEditor.cppm)
