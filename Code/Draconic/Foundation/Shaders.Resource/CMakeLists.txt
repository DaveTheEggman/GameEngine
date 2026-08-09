# Foundation::Shaders.Resource - `draconic.shaders.resource` (shaders as resources: ShaderSource
# -> ShaderResource via ShaderFactory; ties into the resource manager + ShaderSystem).
add_library(Shaders.Resource STATIC)
add_library(Foundation::Shaders.Resource ALIAS Shaders.Resource)

target_link_libraries(Shaders.Resource
    PUBLIC Foundation::Core Foundation::RHI Foundation::Shaders Foundation::Shaders.System
           Foundation::Resource Foundation::Content Foundation::Policy)

target_sources(Shaders.Resource
    PUBLIC
        FILE_SET shader_resource_modules TYPE CXX_MODULES FILES
            ShaderResource.cppm)

