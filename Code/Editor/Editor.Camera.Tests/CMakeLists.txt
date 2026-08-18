add_executable(Editor.Camera.Tests TestsMain.cpp EditorCameraTests.cpp)
target_link_libraries(Editor.Camera.Tests PRIVATE
    Editor::Camera Foundation::Shell Foundation::Core doctest::doctest)
target_compile_definitions(Editor.Camera.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Camera.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Camera.Tests COMMAND Editor.Camera.Tests)
