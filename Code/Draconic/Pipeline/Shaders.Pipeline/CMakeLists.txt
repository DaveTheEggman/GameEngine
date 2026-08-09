# Draconic::Shaders.Pipeline - `draconic.shaders.editor` (tooling: ShaderAsset -> ShaderSource
# cook). Reads .hlsl text through the VFS; never linked by the runtime.
add_library(Draconic.Shaders.Pipeline STATIC)
add_library(Draconic::Shaders.Pipeline ALIAS Draconic.Shaders.Pipeline)

target_link_libraries(Draconic.Shaders.Pipeline
    PUBLIC Draconic::Core Draconic::Pipeline.Core Draconic::VFS Draconic::Shaders.Resource
           Draconic::Content Draconic::Policy)

target_sources(Draconic.Shaders.Pipeline
    PRIVATE
        ShaderAssetImpl.cpp
    PUBLIC
        FILE_SET shader_editor_modules TYPE CXX_MODULES FILES
            ShaderAsset.cppm)

