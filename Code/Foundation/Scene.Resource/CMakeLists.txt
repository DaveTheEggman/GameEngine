# Foundation::Scene.Resource - `foundation.scene.resource` (whole-scene serialization +
# runtime LoadScene from a content-DB instance).
add_library(Scene.Resource STATIC)
add_library(Foundation::Scene.Resource ALIAS Scene.Resource)

target_link_libraries(Scene.Resource
    PUBLIC Foundation::Core Foundation::Content Foundation::Scene Foundation::Policy
           Foundation::Xml.Serialization)

target_sources(Scene.Resource
    PRIVATE
        SceneResourceImpl.cpp   # out-of-line public-API defs (see SceneResource.cppm)
    PUBLIC
        FILE_SET scene_resource_modules TYPE CXX_MODULES FILES
            SceneResource.cppm)

