add_executable(Editor.Generic.Tests TestsMain.cpp AssetFormTests.cpp)
target_link_libraries(Editor.Generic.Tests PRIVATE
    Editor::Generic Editor::Core Foundation::Content doctest::doctest)
target_compile_definitions(Editor.Generic.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Generic.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Generic.Tests COMMAND Editor.Generic.Tests)
