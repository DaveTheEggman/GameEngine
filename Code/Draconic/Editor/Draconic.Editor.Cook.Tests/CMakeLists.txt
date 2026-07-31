add_executable(Draconic.Editor.Cook.Tests TestsMain.cpp CookDriverTests.cpp)
target_link_libraries(Draconic.Editor.Cook.Tests PRIVATE Draconic::Editor.Cook doctest::doctest)
target_compile_definitions(Draconic.Editor.Cook.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Editor.Cook.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Editor.Cook.Tests COMMAND Draconic.Editor.Cook.Tests)
