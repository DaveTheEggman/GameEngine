# Foundation::Mcp - `foundation.mcp` (JSON-RPC 2.0 + MCP lifecycle + tool/resource registry).
# The MCP protocol layer: transport-abstract (stdio + in-memory), implements the v1
# subset. Depends on Json (the wire codec) + Core.

util_add_engine_library(Mcp ALIAS Foundation::Mcp)
target_link_libraries(Mcp PUBLIC Foundation::Json Foundation::Core Foundation::Policy)

target_sources(Mcp
    PUBLIC
        FILE_SET mcp_modules TYPE CXX_MODULES FILES
            Schema.cppm
            Server.cppm
            Transport.cppm
            McpModule.cppm)
