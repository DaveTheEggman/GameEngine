# Raptor::ShaderResource — `raptor.shaders.resource` (shaders as resources: ShaderSource
# -> ShaderResource via ShaderFactory; ties into the resource manager + ShaderSystem).
add_library(RaptorShaderResource STATIC)
add_library(Raptor::ShaderResource ALIAS RaptorShaderResource)

target_link_libraries(RaptorShaderResource
    PUBLIC Raptor::Core Raptor::RHI Raptor::Shaders Raptor::ShaderSystem
           Raptor::Resource Raptor::Content Raptor::Policy)

target_sources(RaptorShaderResource
    PUBLIC
        FILE_SET shader_resource_modules TYPE CXX_MODULES FILES
            ShaderResource.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
