add_executable(Mcp.Reflection.Tests TestsMain.cpp McpReflectionTests.cpp)
target_link_libraries(Mcp.Reflection.Tests PRIVATE Foundation::Mcp.Reflection doctest::doctest)
target_compile_definitions(Mcp.Reflection.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Mcp.Reflection.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Mcp.Reflection.Tests COMMAND Mcp.Reflection.Tests)
