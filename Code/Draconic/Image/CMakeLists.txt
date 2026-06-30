# Draconic::Image — `draconic.image` (CPU-side image data + manipulation).
add_library(DraconicImage STATIC)
add_library(Draconic::Image ALIAS DraconicImage)
target_link_libraries(DraconicImage PUBLIC Draconic::Core Draconic::Policy)
target_sources(DraconicImage PUBLIC FILE_SET image_modules TYPE CXX_MODULES FILES
    PixelFormat.cppm
    ImageData.cppm
    Image.cppm
    NineSlice.cppm
    AtlasBuilder.cppm
    ImageModule.cppm)
add_subdirectory(IO)
add_subdirectory(Resource)  # draconic.image.resource (runtime CPU image)
add_subdirectory(Editor)    # draconic.image.editor (tooling)
if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
