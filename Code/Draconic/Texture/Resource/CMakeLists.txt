# Draconic::TextureResource - `draconic.texture.resource` (runtime cooked record + GPU factory).
add_library(DraconicTextureResource STATIC)
add_library(Draconic::TextureResource ALIAS DraconicTextureResource)
target_link_libraries(DraconicTextureResource PUBLIC Draconic::Core Draconic::RHI Draconic::Texture Draconic::Content Draconic::Resource Draconic::Policy)
target_sources(DraconicTextureResource PUBLIC FILE_SET texture_resource_modules TYPE CXX_MODULES FILES TextureResource.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
