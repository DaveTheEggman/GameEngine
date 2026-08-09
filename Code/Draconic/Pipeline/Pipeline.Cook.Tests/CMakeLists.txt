add_executable(Draconic.Pipeline.Cook.Tests TestsMain.cpp CookDriverTests.cpp)
target_link_libraries(Draconic.Pipeline.Cook.Tests PRIVATE Draconic::Pipeline.Cook doctest::doctest)
target_compile_definitions(Draconic.Pipeline.Cook.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Pipeline.Cook.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Pipeline.Cook.Tests COMMAND Draconic.Pipeline.Cook.Tests)
