add_executable(Json.Tests TestsMain.cpp JsonTests.cpp)
target_link_libraries(Json.Tests PRIVATE Foundation::Json doctest::doctest)
target_compile_definitions(Json.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Json.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Json.Tests COMMAND Json.Tests)
