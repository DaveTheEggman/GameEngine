# Draconic::TextureResource - `draconic.texture.resource` (runtime cooked record + GPU factory).
add_library(Draconic.Texture.Resource STATIC)
add_library(Draconic::TextureResource ALIAS Draconic.Texture.Resource)
target_link_libraries(Draconic.Texture.Resource PUBLIC Draconic::Core Draconic::RHI Draconic::Texture Draconic::Content Draconic::Resource Draconic::Policy)
target_sources(Draconic.Texture.Resource PUBLIC FILE_SET texture_resource_modules TYPE CXX_MODULES FILES TextureResource.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Texture.Resource.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
