# Foundation::Script.Wren unit tests (doctest).

add_executable(Script.Wren.Tests
    TestsMain.cpp
    ScriptWrenTests.cpp)

target_link_libraries(Script.Wren.Tests PRIVATE Foundation::Script.Wren doctest::doctest)

target_compile_definitions(Script.Wren.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

# doctest 2.4.11 <ciso646> deprecation #warning under C++23 - third-party noise.
target_compile_options(Script.Wren.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Script.Wren.Tests COMMAND Script.Wren.Tests)
