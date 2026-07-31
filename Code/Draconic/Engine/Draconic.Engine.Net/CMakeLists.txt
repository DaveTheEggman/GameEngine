# Draconic::NetSubsystem - `draconic.engine.net`: the once-per-context NetworkSubsystem that
# injects the NetworkComponentManager into scenes (ISceneAware) + registers replicated-component
# reflection. The scene-integration half of networking; the per-instance endpoint is NetManager.
add_library(Draconic.Engine.Net STATIC)
add_library(Draconic::NetSubsystem ALIAS Draconic.Engine.Net)

target_link_libraries(Draconic.Engine.Net
    PUBLIC Draconic::Core Draconic::Runtime Draconic::Scene Draconic::SceneSubsystem
           Draconic::NetReplication Draconic::Policy)

target_sources(Draconic.Engine.Net
    PUBLIC
        FILE_SET net_subsystem_modules TYPE CXX_MODULES FILES
            NetworkSubsystem.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Engine.Net.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
