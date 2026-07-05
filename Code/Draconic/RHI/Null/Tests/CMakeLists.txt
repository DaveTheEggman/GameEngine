# Draconic::RHINull unit tests (doctest).

add_executable(DraconicRHINullTests
    TestsMain.cpp
    NullRhiTests.cpp)

target_link_libraries(DraconicRHINullTests PRIVATE Draconic::RHINull doctest::doctest)

target_compile_definitions(DraconicRHINullTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicRHINullTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicRHINullTests COMMAND DraconicRHINullTests)
