# Foundation::Image - `foundation.image` (CPU-side image data + manipulation).
util_add_engine_library(Image ALIAS Foundation::Image)
target_link_libraries(Image PUBLIC Foundation::Core Foundation::Policy)
target_sources(Image
    PRIVATE
        ImageReflectionImpl.cpp
    PUBLIC FILE_SET image_modules TYPE CXX_MODULES FILES
        PixelFormat.cppm
        ImageData.cppm
        Image.cppm
        NineSlice.cppm
        AtlasBuilder.cppm
        ImageModule.cppm)
