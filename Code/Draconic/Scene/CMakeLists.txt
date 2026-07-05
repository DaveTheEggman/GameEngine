# Draconic::Scene - `draconic.scene`, the scene / ECS foundation (entities, transform
# hierarchy, components + per-scene systems, SceneSubsystem). Pure data + logic on
# top of Core; renderer-agnostic.
add_library(DraconicScene STATIC)
add_library(Draconic::Scene ALIAS DraconicScene)

target_link_libraries(DraconicScene
    PUBLIC Draconic::Core Draconic::Policy)

target_sources(DraconicScene
    PUBLIC
        FILE_SET scene_modules TYPE CXX_MODULES FILES
            Entity.cppm
            Phase.cppm
            System.cppm
            Component.cppm
            Aware.cppm
            Scene.cppm
            SceneModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# The Context-level scene driver + ISceneAware broker (bridges Scene + Runtime).
add_subdirectory(Subsystem)

# Whole-scene serialization: runtime load (Resource) + tooling save (Editor).
add_subdirectory(Resource)
add_subdirectory(Editor)
