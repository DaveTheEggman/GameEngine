# Draconic::Fonts — `draconic.fonts` (font/glyph/text-layout types + interfaces).
# Ported from Sedulous.Fonts (excluding Fonts.Resources).

add_library(DraconicFonts STATIC)
add_library(Draconic::Fonts ALIAS DraconicFonts)

target_link_libraries(DraconicFonts PUBLIC Draconic::Core Draconic::Image Draconic::Policy)

target_sources(DraconicFonts
    PUBLIC
        FILE_SET fonts_modules TYPE CXX_MODULES FILES
            Types.cppm
            Interfaces.cppm
            TextUtil.cppm
            FontAtlasTexture.cppm
            NullFontService.cppm
            FontsModule.cppm)

# Sedulous draws each of these as its own library; mirror those module lines.
add_subdirectory(Baked)     # draconic.fonts.baked  — rasterizer-free IFont/IFontAtlas
add_subdirectory(IO)        # draconic.fonts.io     — parser/baker pipeline + FontManager
add_subdirectory(TTF)       # draconic.fonts.ttf    — stb_truetype backend + VFS service
add_subdirectory(Importer)  # draconic.fonts.importer — editor-time TTF -> baked

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
