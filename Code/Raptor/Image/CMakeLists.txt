# Raptor::Image — `raptor.image` (CPU-side image data + manipulation).
add_library(RaptorImage STATIC)
add_library(Raptor::Image ALIAS RaptorImage)
target_link_libraries(RaptorImage PUBLIC Raptor::Core Raptor::Policy)
target_sources(RaptorImage PUBLIC FILE_SET image_modules TYPE CXX_MODULES FILES
    PixelFormat.cppm
    ImageData.cppm
    Image.cppm
    NineSlice.cppm
    ImageModule.cppm)
add_subdirectory(IO)
add_subdirectory(Resource)  # raptor.image.resource (runtime CPU image)
add_subdirectory(Editor)    # raptor.image.editor (tooling)
if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
