add_executable(Draconic.Editor.Texture.Tests TestsMain.cpp TextureEditorPageTests.cpp)
target_link_libraries(Draconic.Editor.Texture.Tests PRIVATE
    Draconic::Editor.Texture Draconic::Editor.Core
    Draconic::Texture Draconic::Texture.Pipeline Draconic::Image Draconic::Image.IO
    Draconic::Content Draconic::VFS doctest::doctest)
target_compile_definitions(Draconic.Editor.Texture.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Editor.Texture.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Editor.Texture.Tests COMMAND Draconic.Editor.Texture.Tests)
