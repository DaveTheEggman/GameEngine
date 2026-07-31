# Draconic::SceneResource - `draconic.scene.resource` (whole-scene serialization +
# runtime LoadScene from a content-DB instance).
add_library(Draconic.Scene.Resource STATIC)
add_library(Draconic::SceneResource ALIAS Draconic.Scene.Resource)

target_link_libraries(Draconic.Scene.Resource
    PUBLIC Draconic::Core Draconic::Content Draconic::Scene Draconic::Policy
           Draconic::XmlSerialization)

target_sources(Draconic.Scene.Resource
    PRIVATE
        SceneResourceImpl.cpp   # out-of-line public-API defs (see SceneResource.cppm)
    PUBLIC
        FILE_SET scene_resource_modules TYPE CXX_MODULES FILES
            SceneResource.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Scene.Resource.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
