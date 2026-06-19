# Raptor::TexturesResource — `raptor.textures.resource`.
# The texture as a managed content/resource (source TextureResource -> product Texture).

add_library(RaptorTexturesResource STATIC)
add_library(Raptor::TexturesResource ALIAS RaptorTexturesResource)

target_link_libraries(RaptorTexturesResource
    PUBLIC Raptor::Core Raptor::Textures Raptor::Image Raptor::Content Raptor::Resource Raptor::Policy)

target_sources(RaptorTexturesResource
    PUBLIC
        FILE_SET textures_resource_modules TYPE CXX_MODULES FILES
            TextureResource.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
