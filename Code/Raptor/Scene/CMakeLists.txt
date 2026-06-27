# Raptor::Scene — `raptor.scene`, the scene / ECS foundation (entities, transform
# hierarchy, components + per-scene systems, SceneSubsystem). Pure data + logic on
# top of Core; renderer-agnostic.
add_library(RaptorScene STATIC)
add_library(Raptor::Scene ALIAS RaptorScene)

target_link_libraries(RaptorScene
    PUBLIC Raptor::Core Raptor::Policy)

target_sources(RaptorScene
    PUBLIC
        FILE_SET scene_modules TYPE CXX_MODULES FILES
            Entity.cppm
            Scene.cppm
            SceneModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
