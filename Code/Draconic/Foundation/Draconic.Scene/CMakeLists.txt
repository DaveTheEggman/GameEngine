# Draconic::Scene - `draconic.scene`, the scene / ECS foundation (entities, transform
# hierarchy, components + per-scene systems, SceneSubsystem). Pure data + logic on
# top of Core; renderer-agnostic.
add_library(Draconic.Scene STATIC)
add_library(Draconic::Scene ALIAS Draconic.Scene)

target_link_libraries(Draconic.Scene
    PUBLIC Draconic::Core Draconic::Resource Draconic::Policy)

target_sources(Draconic.Scene
    PRIVATE
        SceneImpl.cpp   # out-of-line Scene member definitions (see Scene.cppm)
    PUBLIC
        FILE_SET scene_modules TYPE CXX_MODULES FILES
            Entity.cppm
            Phase.cppm
            SceneSystem.cppm
            Component.cppm
            Aware.cppm
            Scene.cppm
            SceneManager.cppm
            SceneModule.cppm)

# The Context-level scene driver + ISceneAware broker (bridges Scene + Runtime). Links only the
# base runtime lib, so it climbs wherever Runtime is configured (web included).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/Draconic.Engine.Scene ${CMAKE_CURRENT_BINARY_DIR}/Subsystem)

# Whole-scene serialization: runtime load (Resource) + tooling save (Editor).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Scene.Resource ${CMAKE_CURRENT_BINARY_DIR}/Resource)
if(DRACONIC_BUILD_TOOLS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../Editor/Draconic.Scene.Editor ${CMAKE_CURRENT_BINARY_DIR}/Editor)
endif()
