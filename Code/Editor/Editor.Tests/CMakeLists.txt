
add_executable(Editor.Tests TestsMain.cpp EditorTests.cpp)
target_link_libraries(Editor.Tests PRIVATE Pipeline::Core Foundation::Content doctest::doctest)
target_compile_definitions(Editor.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Tests COMMAND Editor.Tests)
