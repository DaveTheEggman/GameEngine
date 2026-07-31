add_executable(Draconic.Editor.App.Tests TestsMain.cpp ShellTests.cpp LogViewTests.cpp)
target_link_libraries(Draconic.Editor.App.Tests PRIVATE Draconic::Editor.App Draconic::Editor.Core doctest::doctest)
target_compile_definitions(Draconic.Editor.App.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Editor.App.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Editor.App.Tests COMMAND Draconic.Editor.App.Tests)
