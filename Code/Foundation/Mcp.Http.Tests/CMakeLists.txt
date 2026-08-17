add_executable(Mcp.Http.Tests TestsMain.cpp McpHttpTests.cpp)
target_link_libraries(Mcp.Http.Tests PRIVATE
    Foundation::Mcp.Http Foundation::Http Foundation::Net Foundation::Mcp Foundation::Json
    Foundation::Core doctest::doctest)
target_compile_definitions(Mcp.Http.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Mcp.Http.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Mcp.Http.Tests COMMAND Mcp.Http.Tests)
