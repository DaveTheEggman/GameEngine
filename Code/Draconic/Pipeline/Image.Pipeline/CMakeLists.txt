# Pipeline::Image - `image.pipeline` (tooling: ImageAsset + builder).
add_library(Image.Pipeline STATIC)
add_library(Pipeline::Image ALIAS Image.Pipeline)
target_link_libraries(Image.Pipeline PUBLIC Foundation::Core Pipeline::Core Foundation::Image Foundation::Image.IO Foundation::Image.Resource Foundation::Content Foundation::Policy)
target_sources(Image.Pipeline
    PRIVATE
        ImageAssetImpl.cpp
    PUBLIC FILE_SET image_editor_modules TYPE CXX_MODULES FILES ImageAsset.cppm)
