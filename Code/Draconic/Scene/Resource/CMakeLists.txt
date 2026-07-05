# Draconic::SceneResource - `draconic.scene.resource` (whole-scene serialization +
# runtime LoadScene from a content-DB instance).
add_library(DraconicSceneResource STATIC)
add_library(Draconic::SceneResource ALIAS DraconicSceneResource)

target_link_libraries(DraconicSceneResource
    PUBLIC Draconic::Core Draconic::Content Draconic::Scene Draconic::Policy)

target_sources(DraconicSceneResource
    PUBLIC
        FILE_SET scene_resource_modules TYPE CXX_MODULES FILES
            SceneResource.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
