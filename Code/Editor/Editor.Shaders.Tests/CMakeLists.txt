# Editor::Shaders tests: the HLSL lexer's registration and classification; the shader page's
# preview slot.
add_executable(Editor.Shaders.Tests TestsMain.cpp HlslLexerTests.cpp PreviewShaderSlotTests.cpp)
target_link_libraries(Editor.Shaders.Tests PRIVATE
    Editor::Shaders Foundation::UI.Toolkit Foundation::Core Foundation::Shaders.System
    Foundation::RHI.Null doctest::doctest)
target_compile_definitions(Editor.Shaders.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Shaders.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Shaders.Tests COMMAND Editor.Shaders.Tests)
