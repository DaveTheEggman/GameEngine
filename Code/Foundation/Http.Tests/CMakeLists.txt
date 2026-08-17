add_executable(Http.Tests TestsMain.cpp HttpTests.cpp)
target_link_libraries(Http.Tests PRIVATE
    Foundation::Http Foundation::Net Foundation::Core doctest::doctest)
target_compile_definitions(Http.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Http.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Http.Tests COMMAND Http.Tests)
