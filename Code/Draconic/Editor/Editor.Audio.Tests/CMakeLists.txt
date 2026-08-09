add_executable(Editor.Audio.Tests TestsMain.cpp BusLayoutPageTests.cpp)
target_link_libraries(Editor.Audio.Tests PRIVATE
    Editor::Audio Editor::Core
    Foundation::Audio Pipeline::Audio Foundation::Content doctest::doctest)
target_compile_definitions(Editor.Audio.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Audio.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Audio.Tests COMMAND Editor.Audio.Tests)
