# Draconic::Materials.Resource - `draconic.materials.resource` (materials as resources:
# MaterialSource -> Material via MaterialFactory; depends on the ShaderResource).
add_library(Draconic.Materials.Resource STATIC)
add_library(Draconic::Materials.Resource ALIAS Draconic.Materials.Resource)

target_link_libraries(Draconic.Materials.Resource
    PUBLIC Draconic::Core Draconic::Resource Draconic::Content Draconic::Shaders
           Draconic::Texture Draconic::Texture.Resource
           Draconic::Shaders.Resource Draconic::Materials Draconic::Policy)

target_sources(Draconic.Materials.Resource
    PUBLIC
        FILE_SET material_resource_modules TYPE CXX_MODULES FILES
            MaterialResource.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Materials.Resource.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
