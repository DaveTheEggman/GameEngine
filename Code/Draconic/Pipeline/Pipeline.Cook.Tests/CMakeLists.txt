add_executable(Pipeline.Cook.Tests TestsMain.cpp CookDriverTests.cpp)
target_link_libraries(Pipeline.Cook.Tests PRIVATE Pipeline::Cook doctest::doctest)
target_compile_definitions(Pipeline.Cook.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Pipeline.Cook.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Pipeline.Cook.Tests COMMAND Pipeline.Cook.Tests)
