# Raptor::ShaderEditor — `raptor.shaders.editor` (tooling: ShaderAsset -> ShaderSource
# cook). Reads .hlsl text through the VFS; never linked by the runtime.
add_library(RaptorShaderEditor STATIC)
add_library(Raptor::ShaderEditor ALIAS RaptorShaderEditor)

target_link_libraries(RaptorShaderEditor
    PUBLIC Raptor::Core Raptor::Editor Raptor::VFS Raptor::ShaderResource
           Raptor::Content Raptor::Policy)

target_sources(RaptorShaderEditor
    PUBLIC
        FILE_SET shader_editor_modules TYPE CXX_MODULES FILES
            ShaderAsset.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
