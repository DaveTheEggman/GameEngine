# Draconic::Shaders.Editor - `draconic.shaders.editor` (tooling: ShaderAsset -> ShaderSource
# cook). Reads .hlsl text through the VFS; never linked by the runtime.
add_library(Draconic.Shaders.Editor STATIC)
add_library(Draconic::Shaders.Editor ALIAS Draconic.Shaders.Editor)

target_link_libraries(Draconic.Shaders.Editor
    PUBLIC Draconic::Core Draconic::Editor Draconic::VFS Draconic::Shaders.Resource
           Draconic::Content Draconic::Policy)

target_sources(Draconic.Shaders.Editor
    PUBLIC
        FILE_SET shader_editor_modules TYPE CXX_MODULES FILES
            ShaderAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Shaders.Editor.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
