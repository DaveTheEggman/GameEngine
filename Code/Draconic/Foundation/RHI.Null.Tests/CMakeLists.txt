# Foundation::RHI.Null unit tests (doctest).

add_executable(RHI.Null.Tests
    TestsMain.cpp
    NullRhiTests.cpp)

target_link_libraries(RHI.Null.Tests PRIVATE Foundation::RHI.Null doctest::doctest)

target_compile_definitions(RHI.Null.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(RHI.Null.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME RHI.Null.Tests COMMAND RHI.Null.Tests)
