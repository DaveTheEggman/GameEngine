add_executable(Editor.Heightfield.Tests TestsMain.cpp HeightfieldEditorPageTests.cpp HeightfieldThumbnailTests.cpp)
target_link_libraries(Editor.Heightfield.Tests PRIVATE
    Editor::Heightfield Editor::Core
    Pipeline::Heightfield Foundation::VFS Foundation::Content doctest::doctest)
target_compile_definitions(Editor.Heightfield.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Heightfield.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Heightfield.Tests COMMAND Editor.Heightfield.Tests)
