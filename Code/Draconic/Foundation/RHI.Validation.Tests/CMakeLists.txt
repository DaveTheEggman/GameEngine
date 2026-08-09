# Foundation::RHI.Validation unit tests (doctest). Wraps the Null backend.

add_executable(RHI.Validation.Tests
    TestsMain.cpp
    ValidationTests.cpp)

target_link_libraries(RHI.Validation.Tests PRIVATE
    Foundation::RHI.Validation Foundation::RHI.Null doctest::doctest)

target_compile_definitions(RHI.Validation.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(RHI.Validation.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME RHI.Validation.Tests COMMAND RHI.Validation.Tests)
