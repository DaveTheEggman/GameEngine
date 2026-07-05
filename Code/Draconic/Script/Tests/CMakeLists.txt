# Draconic::Script unit tests (doctest). Reuses the doctest fetched by the Core
# tests if present; otherwise fetches it the same way.

add_executable(DraconicScriptTests
    TestsMain.cpp
    ScriptTests.cpp)

target_link_libraries(DraconicScriptTests PRIVATE Draconic::Script doctest::doctest)

target_compile_definitions(DraconicScriptTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

# doctest 2.4.11 <ciso646> deprecation #warning under C++23 - third-party noise.
target_compile_options(DraconicScriptTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicScriptTests COMMAND DraconicScriptTests)
