# Foundation::Script.AngelScript unit tests (doctest). Links the Wren backend too:
# the registry test proves both backends resolve side by side.

add_executable(Script.AngelScript.Tests
    TestsMain.cpp
    ScriptAngelScriptTests.cpp)

target_link_libraries(Script.AngelScript.Tests PRIVATE
    Foundation::Script.AngelScript Foundation::Script.Wren doctest::doctest)

target_compile_definitions(Script.AngelScript.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

# doctest 2.4.11 <ciso646> deprecation #warning under C++23 - third-party noise.
target_compile_options(Script.AngelScript.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Script.AngelScript.Tests COMMAND Script.AngelScript.Tests)
