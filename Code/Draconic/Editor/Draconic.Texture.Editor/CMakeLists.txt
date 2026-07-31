# Draconic::TextureEditor - `draconic.texture.editor` (tooling: TextureAsset + builder + importer).
add_library(Draconic.Texture.Editor STATIC)
add_library(Draconic::TextureEditor ALIAS Draconic.Texture.Editor)
target_link_libraries(Draconic.Texture.Editor PUBLIC
    Draconic::Core Draconic::Editor Draconic::EditorCore Draconic::RHI Draconic::Texture Draconic::TextureResource
    Draconic::Image Draconic::ImageIO Draconic::Content Draconic::Policy)
target_sources(Draconic.Texture.Editor PUBLIC FILE_SET texture_editor_modules TYPE CXX_MODULES FILES TextureAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Texture.Editor.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
