add_executable(Editor.Texture.Tests TestsMain.cpp TextureEditorPageTests.cpp)
target_link_libraries(Editor.Texture.Tests PRIVATE
    Editor::Texture Editor::Core
    Foundation::Texture Pipeline::Texture Foundation::Image Foundation::Image.IO
    Foundation::Content Foundation::VFS doctest::doctest)
target_compile_definitions(Editor.Texture.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Texture.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Texture.Tests COMMAND Editor.Texture.Tests)
