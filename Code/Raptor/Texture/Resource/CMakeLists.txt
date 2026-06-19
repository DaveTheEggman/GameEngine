# Raptor::TextureResource — `raptor.texture.resource` (runtime cooked record + GPU factory).
add_library(RaptorTextureResource STATIC)
add_library(Raptor::TextureResource ALIAS RaptorTextureResource)
target_link_libraries(RaptorTextureResource PUBLIC Raptor::Core Raptor::RHI Raptor::Texture Raptor::Content Raptor::Resource Raptor::Policy)
target_sources(RaptorTextureResource PUBLIC FILE_SET texture_resource_modules TYPE CXX_MODULES FILES TextureResource.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
