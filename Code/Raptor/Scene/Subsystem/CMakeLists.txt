# Raptor::SceneSubsystem — `raptor.scene.subsystem` (the Context-level scene driver +
# ISceneAware broker). Bridges raptor.scene and raptor.runtime; keeps scene core
# runtime-free.
add_library(RaptorSceneSubsystem STATIC)
add_library(Raptor::SceneSubsystem ALIAS RaptorSceneSubsystem)

target_link_libraries(RaptorSceneSubsystem
    PUBLIC Raptor::Core Raptor::Runtime Raptor::Scene Raptor::Policy)

target_sources(RaptorSceneSubsystem
    PUBLIC
        FILE_SET scene_subsystem_modules TYPE CXX_MODULES FILES
            SceneSubsystem.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
