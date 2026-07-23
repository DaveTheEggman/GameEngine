# Draconic::NetSubsystem - `draconic.net.subsystem`: the once-per-context NetworkSubsystem that
# injects the NetworkComponentManager into scenes (ISceneAware) + registers replicated-component
# reflection. The scene-integration half of networking; the per-instance endpoint is NetManager.
add_library(DraconicNetSubsystem STATIC)
add_library(Draconic::NetSubsystem ALIAS DraconicNetSubsystem)

target_link_libraries(DraconicNetSubsystem
    PUBLIC Draconic::Core Draconic::Runtime Draconic::Scene Draconic::SceneSubsystem
           Draconic::NetReplication Draconic::Policy)

target_sources(DraconicNetSubsystem
    PUBLIC
        FILE_SET net_subsystem_modules TYPE CXX_MODULES FILES
            NetworkSubsystem.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
