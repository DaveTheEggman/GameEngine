# Draconic::ShaderResource - `draconic.shaders.resource` (shaders as resources: ShaderSource
# -> ShaderResource via ShaderFactory; ties into the resource manager + ShaderSystem).
add_library(Draconic.Shaders.Resource STATIC)
add_library(Draconic::ShaderResource ALIAS Draconic.Shaders.Resource)

target_link_libraries(Draconic.Shaders.Resource
    PUBLIC Draconic::Core Draconic::RHI Draconic::Shaders Draconic::ShaderSystem
           Draconic::Resource Draconic::Content Draconic::Policy)

target_sources(Draconic.Shaders.Resource
    PUBLIC
        FILE_SET shader_resource_modules TYPE CXX_MODULES FILES
            ShaderResource.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Shaders.Resource.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
