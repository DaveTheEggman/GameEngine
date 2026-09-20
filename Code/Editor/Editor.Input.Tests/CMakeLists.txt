add_executable(Editor.Input.Tests TestsMain.cpp InputMapEditTests.cpp)
target_link_libraries(Editor.Input.Tests PRIVATE
    Editor::Input Editor::Core Foundation::Input Pipeline::Input
    Foundation::Runtime Foundation::Runtime.Client Foundation::Shell Foundation::Graphics doctest::doctest)
target_compile_definitions(Editor.Input.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Input.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Input.Tests COMMAND Editor.Input.Tests)
