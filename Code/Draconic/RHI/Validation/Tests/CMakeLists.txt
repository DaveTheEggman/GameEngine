# Draconic::RHIValidation unit tests (doctest). Wraps the Null backend.

add_executable(DraconicRHIValidationTests
    TestsMain.cpp
    ValidationTests.cpp)

target_link_libraries(DraconicRHIValidationTests PRIVATE
    Draconic::RHIValidation Draconic::RHINull doctest::doctest)

target_compile_definitions(DraconicRHIValidationTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicRHIValidationTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicRHIValidationTests COMMAND DraconicRHIValidationTests)
