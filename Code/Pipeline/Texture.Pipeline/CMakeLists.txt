# Pipeline::Texture - `texture.pipeline` (tooling: TextureAsset + builder + importer).
add_library(Texture.Pipeline STATIC)
add_library(Pipeline::Texture ALIAS Texture.Pipeline)
target_link_libraries(Texture.Pipeline PUBLIC
    Foundation::Core Pipeline::Core Pipeline::Importer Foundation::RHI Foundation::Texture Foundation::Texture.Resource
    Foundation::Image Foundation::Image.IO Foundation::Content Foundation::Policy)
target_sources(Texture.Pipeline
    PRIVATE
        TextureAssetImpl.cpp
    PUBLIC FILE_SET texture_editor_modules TYPE CXX_MODULES FILES TextureAsset.cppm)

