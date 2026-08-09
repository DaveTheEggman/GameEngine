# Foundation::Mcp.Script - `foundation.mcp.script`: the scripting MCP tools (script_api). A module's
# own tool contribution against foundation.mcp; resolves backends live from the ScriptBackendRegistry
# (does NOT link any backend - the host registers those).

add_library(Mcp.Script STATIC)
add_library(Foundation::Mcp.Script ALIAS Mcp.Script)

target_link_libraries(Mcp.Script PUBLIC
    Foundation::Mcp Foundation::Json Foundation::Script Foundation::Script.Facades
    Foundation::Core Foundation::Policy)

target_sources(Mcp.Script
    PUBLIC
        FILE_SET mcp_script_modules TYPE CXX_MODULES FILES
            ScriptTools.cppm)
