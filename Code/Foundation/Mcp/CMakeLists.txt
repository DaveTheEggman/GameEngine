# Foundation::Mcp - `foundation.mcp` (JSON-RPC 2.0 + MCP lifecycle + tool/resource registry).
# The protocol layer of the MCP track: transport-abstract (stdio + in-memory), implements the v1
# subset from docs/specs/mcp-agent-access.md. Depends on Json (the wire codec) + Core.

add_library(Mcp STATIC)
add_library(Foundation::Mcp ALIAS Mcp)

target_link_libraries(Mcp PUBLIC Foundation::Json Foundation::Core Foundation::Policy)

target_sources(Mcp
    PUBLIC
        FILE_SET mcp_modules TYPE CXX_MODULES FILES
            Schema.cppm
            Server.cppm
            Transport.cppm
            McpModule.cppm)
