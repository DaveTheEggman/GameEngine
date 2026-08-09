add_executable(Mcp.Script.Tests TestsMain.cpp ScriptToolsTests.cpp)
target_link_libraries(Mcp.Script.Tests PRIVATE
    Foundation::Mcp.Script Foundation::Script.Wren doctest::doctest)
target_compile_definitions(Mcp.Script.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Mcp.Script.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Mcp.Script.Tests COMMAND Mcp.Script.Tests)
