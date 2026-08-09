# Tools.Mcp - headless MCP stdio server (P1 host). JSON-RPC over stdin/stdout; logs to stderr.
# v1 surface = the reflection tools (type_list/type_info); grows as modules contribute their tools.

add_executable(Tools.Mcp Main.cpp)
target_link_libraries(Tools.Mcp PRIVATE
    Foundation::Mcp.Reflection Editor::Mcp
    Foundation::Mcp Foundation::Json Foundation::Core Foundation::Policy)
