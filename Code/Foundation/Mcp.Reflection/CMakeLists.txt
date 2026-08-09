# Foundation::Mcp.Reflection - `foundation.mcp.reflection`: the reflection MCP tools (type_list,
# type_info). A module's own tool contribution against foundation.mcp; pure TypeRegistry queries.

add_library(Mcp.Reflection STATIC)
add_library(Foundation::Mcp.Reflection ALIAS Mcp.Reflection)

target_link_libraries(Mcp.Reflection PUBLIC
    Foundation::Mcp Foundation::Json Foundation::Core Foundation::Policy)

target_sources(Mcp.Reflection
    PUBLIC
        FILE_SET mcp_reflection_modules TYPE CXX_MODULES FILES
            ReflectionTools.cppm)
