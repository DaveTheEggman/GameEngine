# Raptor::SceneResource — `raptor.scene.resource` (whole-scene serialization +
# runtime LoadScene from a content-DB instance).
add_library(RaptorSceneResource STATIC)
add_library(Raptor::SceneResource ALIAS RaptorSceneResource)

target_link_libraries(RaptorSceneResource
    PUBLIC Raptor::Core Raptor::Content Raptor::Scene Raptor::Policy)

target_sources(RaptorSceneResource
    PUBLIC
        FILE_SET scene_resource_modules TYPE CXX_MODULES FILES
            SceneResource.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
