# Foundation::Runtime.Client unit tests (doctest).

add_executable(Runtime.Client.Tests
    TestsMain.cpp
    ApplicationTests.cpp)

target_link_libraries(Runtime.Client.Tests PRIVATE
    Foundation::Runtime.Client Foundation::Shell.Null Foundation::Graphics Foundation::Graphics.Null doctest::doctest)

target_compile_definitions(Runtime.Client.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Runtime.Client.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Runtime.Client.Tests COMMAND Runtime.Client.Tests)
