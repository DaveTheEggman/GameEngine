# Draconic::Texture.Pipeline - `draconic.texture.editor` (tooling: TextureAsset + builder + importer).
add_library(Draconic.Texture.Pipeline STATIC)
add_library(Draconic::Texture.Pipeline ALIAS Draconic.Texture.Pipeline)
target_link_libraries(Draconic.Texture.Pipeline PUBLIC
    Draconic::Core Draconic::Pipeline.Core Draconic::Editor.Core Draconic::RHI Draconic::Texture Draconic::Texture.Resource
    Draconic::Image Draconic::Image.IO Draconic::Content Draconic::Policy)
target_sources(Draconic.Texture.Pipeline
    PRIVATE
        TextureAssetImpl.cpp
    PUBLIC FILE_SET texture_editor_modules TYPE CXX_MODULES FILES TextureAsset.cppm)

