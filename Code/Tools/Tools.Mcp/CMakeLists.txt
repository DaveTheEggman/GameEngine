# Tools.Mcp - headless MCP stdio server. JSON-RPC over stdin/stdout; logs to stderr.
# v1 surface = the reflection tools (type_list/type_info); grows as modules contribute their tools.

add_executable(Tools.Mcp Main.cpp)

# Per-executable build stamp (the Runtime.Client pattern): regenerated every build,
# content-compared so no-op builds stay no-op; host_info reports it for stale-binary detection.
set(MCP_BUILD_STAMP_CPP "${CMAKE_CURRENT_BINARY_DIR}/BuildStamp.cpp")
execute_process(COMMAND ${CMAKE_COMMAND}
    -DOUT=${MCP_BUILD_STAMP_CPP} -DSRC_ROOT=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/cmake/GenerateBuildStamp.cmake)
add_custom_target(BuildStamp.Tick.Mcp
    COMMAND ${CMAKE_COMMAND}
        -DOUT=${MCP_BUILD_STAMP_CPP} -DSRC_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateBuildStamp.cmake
    BYPRODUCTS ${MCP_BUILD_STAMP_CPP}
    COMMENT "Refreshing Tools.Mcp build stamp")
add_dependencies(Tools.Mcp BuildStamp.Tick.Mcp)
target_sources(Tools.Mcp PRIVATE ${MCP_BUILD_STAMP_CPP})
target_link_libraries(Tools.Mcp PRIVATE
    Foundation::Mcp.Reflection Foundation::Mcp.Script Editor::Mcp
    Engine::ScriptSurface # complete script surface (all facades); both script backends
                          # (angelscript | luau) come transitively from Pipeline::Registration below
    Engine::SceneSurface # full manager set + component reflection (scene_validate payload parsing)
    Pipeline::Registration Pipeline::Core Pipeline::Importer # the write tools' builder/importer set
    Foundation::Mcp Foundation::Json Foundation::Core Foundation::Policy)
