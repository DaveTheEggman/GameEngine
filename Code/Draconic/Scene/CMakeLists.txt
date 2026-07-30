# Draconic::Scene - `draconic.scene`, the scene / ECS foundation (entities, transform
# hierarchy, components + per-scene systems, SceneSubsystem). Pure data + logic on
# top of Core; renderer-agnostic.
add_library(DraconicScene STATIC)
add_library(Draconic::Scene ALIAS DraconicScene)

target_link_libraries(DraconicScene
    PUBLIC Draconic::Core Draconic::Resource Draconic::Policy)

target_sources(DraconicScene
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

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# The Context-level scene driver + ISceneAware broker (bridges Scene + Runtime). App-layer, so it
# climbs with Runtime; off for web until then. (The base Scene lib below is renderer-agnostic.)
if(NOT EMSCRIPTEN)
    add_subdirectory(Subsystem)
endif()

# Whole-scene serialization: runtime load (Resource) + tooling save (Editor).
add_subdirectory(Resource)
if(DRACONIC_BUILD_TOOLS)
    add_subdirectory(Editor)
endif()
