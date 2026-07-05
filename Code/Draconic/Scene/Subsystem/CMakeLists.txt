# Draconic::SceneSubsystem - `draconic.scene.subsystem` (the Context-level scene driver +
# ISceneAware broker). Bridges draconic.scene and draconic.runtime; keeps scene core
# runtime-free.
add_library(DraconicSceneSubsystem STATIC)
add_library(Draconic::SceneSubsystem ALIAS DraconicSceneSubsystem)

target_link_libraries(DraconicSceneSubsystem
    PUBLIC Draconic::Core Draconic::Runtime Draconic::Scene Draconic::Policy)

target_sources(DraconicSceneSubsystem
    PUBLIC
        FILE_SET scene_subsystem_modules TYPE CXX_MODULES FILES
            SceneSubsystem.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
