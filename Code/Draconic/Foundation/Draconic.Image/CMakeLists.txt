# Draconic::Image - `draconic.image` (CPU-side image data + manipulation).
add_library(Draconic.Image STATIC)
add_library(Draconic::Image ALIAS Draconic.Image)
target_link_libraries(Draconic.Image PUBLIC Draconic::Core Draconic::Policy)
target_sources(Draconic.Image PUBLIC FILE_SET image_modules TYPE CXX_MODULES FILES
    PixelFormat.cppm
    ImageData.cppm
    Image.cppm
    NineSlice.cppm
    AtlasBuilder.cppm
    ImageModule.cppm)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Image.IO ${CMAKE_CURRENT_BINARY_DIR}/IO)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Image.Resource ${CMAKE_CURRENT_BINARY_DIR}/Resource)  # draconic.image.resource (runtime CPU image)
if(DRACONIC_BUILD_TOOLS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../Editor/Draconic.Image.Editor ${CMAKE_CURRENT_BINARY_DIR}/Editor)    # draconic.image.editor (tooling)
endif()
