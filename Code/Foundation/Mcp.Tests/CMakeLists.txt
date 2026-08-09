add_executable(Mcp.Tests TestsMain.cpp McpTests.cpp)
target_link_libraries(Mcp.Tests PRIVATE Foundation::Mcp doctest::doctest)
target_compile_definitions(Mcp.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Mcp.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Mcp.Tests COMMAND Mcp.Tests)
