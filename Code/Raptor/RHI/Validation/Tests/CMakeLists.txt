# Raptor::RHIValidation unit tests (doctest). Wraps the Null backend.
if(NOT TARGET doctest::doctest)
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
        GIT_SHALLOW TRUE
        SYSTEM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

add_executable(RaptorRHIValidationTests
    TestsMain.cpp
    ValidationTests.cpp)

target_link_libraries(RaptorRHIValidationTests PRIVATE
    Raptor::RHIValidation Raptor::RHINull doctest::doctest)

target_compile_definitions(RaptorRHIValidationTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(RaptorRHIValidationTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME RaptorRHIValidationTests COMMAND RaptorRHIValidationTests)
