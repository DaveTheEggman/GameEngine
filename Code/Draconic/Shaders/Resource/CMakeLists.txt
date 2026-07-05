# Draconic::ShaderResource - `draconic.shaders.resource` (shaders as resources: ShaderSource
# -> ShaderResource via ShaderFactory; ties into the resource manager + ShaderSystem).
add_library(DraconicShaderResource STATIC)
add_library(Draconic::ShaderResource ALIAS DraconicShaderResource)

target_link_libraries(DraconicShaderResource
    PUBLIC Draconic::Core Draconic::RHI Draconic::Shaders Draconic::ShaderSystem
           Draconic::Resource Draconic::Content Draconic::Policy)

target_sources(DraconicShaderResource
    PUBLIC
        FILE_SET shader_resource_modules TYPE CXX_MODULES FILES
            ShaderResource.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
