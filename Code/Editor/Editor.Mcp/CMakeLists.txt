# Editor::Mcp - `editor.mcp`: the project MCP tools (project_create/open/info) over foundation.mcp,
# built on the headless EditorProject. A module's own tool contribution; headless (no UI).

add_library(Editor.Mcp STATIC)
add_library(Editor::Mcp ALIAS Editor.Mcp)

target_link_libraries(Editor.Mcp PUBLIC
    Editor::Core Foundation::Mcp Foundation::Json Foundation::Core Foundation::Policy)

target_sources(Editor.Mcp
    PUBLIC
        FILE_SET editor_mcp_modules TYPE CXX_MODULES FILES
            ProjectTools.cppm)
