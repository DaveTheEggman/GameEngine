add_executable(Draconic.Editor.Image.Tests TestsMain.cpp ImageEditorPageTests.cpp)
target_link_libraries(Draconic.Editor.Image.Tests PRIVATE
    Draconic::EditorImage Draconic::EditorCore
    Draconic::Image Draconic::ImageEditor Draconic::Content doctest::doctest)
target_compile_definitions(Draconic.Editor.Image.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Editor.Image.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Editor.Image.Tests COMMAND Draconic.Editor.Image.Tests)
