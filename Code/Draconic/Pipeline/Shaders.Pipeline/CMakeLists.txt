# Pipeline::Shaders - `shaders.pipeline` (tooling: ShaderAsset -> ShaderSource
# cook). Reads .hlsl text through the VFS; never linked by the runtime.
add_library(Shaders.Pipeline STATIC)
add_library(Pipeline::Shaders ALIAS Shaders.Pipeline)

target_link_libraries(Shaders.Pipeline
    PUBLIC Foundation::Core Pipeline::Core Foundation::VFS Foundation::Shaders.Resource
           Foundation::Content Foundation::Policy)

target_sources(Shaders.Pipeline
    PRIVATE
        ShaderAssetImpl.cpp
    PUBLIC
        FILE_SET shader_editor_modules TYPE CXX_MODULES FILES
            ShaderAsset.cppm)

