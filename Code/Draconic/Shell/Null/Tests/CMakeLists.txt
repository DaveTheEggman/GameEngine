# Draconic::ShellNull unit tests (doctest).

add_executable(DraconicShellNullTests
    TestsMain.cpp
    NullShellTests.cpp)

target_link_libraries(DraconicShellNullTests PRIVATE
    Draconic::ShellNull doctest::doctest)

target_compile_definitions(DraconicShellNullTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicShellNullTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicShellNullTests COMMAND DraconicShellNullTests)
