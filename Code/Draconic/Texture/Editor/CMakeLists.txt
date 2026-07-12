# Draconic::TextureEditor - `draconic.texture.editor` (tooling: TextureAsset + builder + importer).
add_library(DraconicTextureEditor STATIC)
add_library(Draconic::TextureEditor ALIAS DraconicTextureEditor)
target_link_libraries(DraconicTextureEditor PUBLIC
    Draconic::Core Draconic::Editor Draconic::EditorCore Draconic::RHI Draconic::Texture Draconic::TextureResource
    Draconic::Image Draconic::ImageIO Draconic::Content Draconic::Policy)
target_sources(DraconicTextureEditor PUBLIC FILE_SET texture_editor_modules TYPE CXX_MODULES FILES TextureAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
