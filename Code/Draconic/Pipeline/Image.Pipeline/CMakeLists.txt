# Draconic::Image.Pipeline - `draconic.image.editor` (tooling: ImageAsset + builder).
add_library(Draconic.Image.Pipeline STATIC)
add_library(Draconic::Image.Pipeline ALIAS Draconic.Image.Pipeline)
target_link_libraries(Draconic.Image.Pipeline PUBLIC Draconic::Core Draconic::Pipeline.Core Draconic::Image Draconic::Image.IO Draconic::Image.Resource Draconic::Content Draconic::Policy)
target_sources(Draconic.Image.Pipeline
    PRIVATE
        ImageAssetImpl.cpp
    PUBLIC FILE_SET image_editor_modules TYPE CXX_MODULES FILES ImageAsset.cppm)
