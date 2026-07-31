# Draconic::Fonts - `draconic.fonts` (font/glyph/text-layout types + interfaces).
# Ported from Sedulous.Fonts (excluding Fonts.Resources).

add_library(Draconic.Fonts STATIC)
add_library(Draconic::Fonts ALIAS Draconic.Fonts)

target_link_libraries(Draconic.Fonts PUBLIC Draconic::Core Draconic::Image Draconic::Policy)

target_sources(Draconic.Fonts
    PUBLIC
        FILE_SET fonts_modules TYPE CXX_MODULES FILES
            Types.cppm
            Interfaces.cppm
            TextUtil.cppm
            FontAtlasTexture.cppm
            NullFontService.cppm
            FontsModule.cppm)

# Sedulous draws each of these as its own library; mirror those module lines.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Fonts.Baked ${CMAKE_CURRENT_BINARY_DIR}/Baked)     # draconic.fonts.baked  - rasterizer-free IFont/IFontAtlas
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Fonts.IO ${CMAKE_CURRENT_BINARY_DIR}/IO)        # draconic.fonts.io     - parser/baker pipeline + FontManager
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Fonts.TTF ${CMAKE_CURRENT_BINARY_DIR}/TTF)       # draconic.fonts.ttf    - stb_truetype backend + VFS service
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Fonts.Importer ${CMAKE_CURRENT_BINARY_DIR}/Importer)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Fonts.DistanceField ${CMAKE_CURRENT_BINARY_DIR}/DistanceField)  # draconic.fonts.importer - editor-time TTF -> baked

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Fonts.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
