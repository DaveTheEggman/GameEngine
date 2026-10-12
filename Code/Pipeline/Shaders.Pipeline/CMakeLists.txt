# Pipeline::Shaders - `shaders.pipeline` (tooling: ShaderAsset -> ShaderSource cook, compiling
# every variant with DXC). Reads .hlsl text through the VFS; never linked by the runtime.
util_add_engine_library(Shaders.Pipeline ALIAS Pipeline::Shaders)
target_link_libraries(Shaders.Pipeline
    PUBLIC Foundation::Core Pipeline::Core Pipeline::Importer Foundation::VFS Foundation::Shaders Foundation::Shaders.System
           Foundation::Shaders.Resource
           Foundation::Content Foundation::Policy)

target_sources(Shaders.Pipeline
    PRIVATE
        ShaderAssetImpl.cpp
        ShaderCreatorsImpl.cpp # File > New, asset_create
    PUBLIC
        FILE_SET shader_editor_modules TYPE CXX_MODULES FILES
            ShaderAsset.cppm
            ShaderCheck.cppm)

