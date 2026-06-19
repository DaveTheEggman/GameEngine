# Raptor::TextureEditor — `raptor.texture.editor` (tooling: TextureAsset + builder + importer).
add_library(RaptorTextureEditor STATIC)
add_library(Raptor::TextureEditor ALIAS RaptorTextureEditor)
target_link_libraries(RaptorTextureEditor PUBLIC
    Raptor::Core Raptor::Editor Raptor::RHI Raptor::Texture Raptor::TextureResource
    Raptor::Image Raptor::ImageIO Raptor::Content Raptor::Policy)
target_sources(RaptorTextureEditor PUBLIC FILE_SET texture_editor_modules TYPE CXX_MODULES FILES TextureAsset.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
