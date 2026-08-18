# Editor::Mcp - `editor.mcp`: the project MCP tools (project_create/open/info) over foundation.mcp,
# built on the headless EditorProject. A module's own tool contribution; headless (no UI).

add_library(Editor.Mcp STATIC)
add_library(Editor::Mcp ALIAS Editor.Mcp)

target_link_libraries(Editor.Mcp PUBLIC
    Editor::Core Foundation::Content Foundation::VFS
    Pipeline::Core Pipeline::Importer Pipeline::Cook Pipeline::Audio # SoundCueAsset (health audit)
    Foundation::Scene Foundation::Scene.Resource Foundation::Xml.Serialization
    Foundation::Resource # factory-less ResourceManager = the scene-Ref collector (asset_uses)
    Pipeline::Script Foundation::Script.Resource # script_validate: the language-cook registry
    Foundation::Script # script_create: backend registry (language -> file extension)
    Engine::SceneSurface # the FULL manager set for scene_validate's scratch (full payload validation)
    Foundation::Mcp Foundation::Json Foundation::Core Foundation::Policy)

target_sources(Editor.Mcp
    PUBLIC
        FILE_SET editor_mcp_modules TYPE CXX_MODULES FILES
            McpSession.cppm
            SceneTools.cppm
            AssetUses.cppm
            ProjectHealth.cppm
            LogTools.cppm
            McpResources.cppm
            ScriptValidate.cppm
            ScriptCreate.cppm
            ProjectExport.cppm
            ProjectTools.cppm)
