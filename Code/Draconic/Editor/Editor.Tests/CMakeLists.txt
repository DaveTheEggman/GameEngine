
add_executable(Draconic.Editor.Tests TestsMain.cpp EditorTests.cpp)
target_link_libraries(Draconic.Editor.Tests PRIVATE Draconic::Pipeline.Core Draconic::Content doctest::doctest)
target_compile_definitions(Draconic.Editor.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Editor.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Editor.Tests COMMAND Draconic.Editor.Tests)
