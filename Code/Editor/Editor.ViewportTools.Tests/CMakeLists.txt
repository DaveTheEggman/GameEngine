add_executable(Editor.ViewportTools.Tests TestsMain.cpp ViewportToolsTests.cpp)
target_link_libraries(Editor.ViewportTools.Tests PRIVATE Editor::ViewportTools Editor::Core
    doctest::doctest)
target_compile_definitions(Editor.ViewportTools.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.ViewportTools.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.ViewportTools.Tests COMMAND Editor.ViewportTools.Tests)
