# Draconic::ShaderEditor — `draconic.shaders.editor` (tooling: ShaderAsset -> ShaderSource
# cook). Reads .hlsl text through the VFS; never linked by the runtime.
add_library(DraconicShaderEditor STATIC)
add_library(Draconic::ShaderEditor ALIAS DraconicShaderEditor)

target_link_libraries(DraconicShaderEditor
    PUBLIC Draconic::Core Draconic::Editor Draconic::VFS Draconic::ShaderResource
           Draconic::Content Draconic::Policy)

target_sources(DraconicShaderEditor
    PUBLIC
        FILE_SET shader_editor_modules TYPE CXX_MODULES FILES
            ShaderAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
