# Foundation::Mcp.Http - `foundation.mcp.http`: the MCP streamable-HTTP binding. Hosts the
# SAME McpServer registry the stdio host serves, over Foundation::Http - localhost + bearer
# token, POST /mcp one message per request (Connection: close), GET /events = the SSE channel
# for server-initiated traffic. A satellite of foundation.mcp (the Mcp.Reflection pattern), so
# the protocol core stays transport-free.

util_add_engine_library(Mcp.Http ALIAS Foundation::Mcp.Http)
target_link_libraries(Mcp.Http PUBLIC
    Foundation::Mcp Foundation::Http Foundation::Core Foundation::Policy)

target_sources(Mcp.Http
    PRIVATE
        McpHttpImpl.cpp
    PUBLIC
        FILE_SET mcp_http_modules TYPE CXX_MODULES FILES
            McpHttp.cppm)
