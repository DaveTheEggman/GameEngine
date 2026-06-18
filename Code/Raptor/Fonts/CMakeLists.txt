# Raptor::Fonts — `raptor.fonts` (font/glyph/text-layout types + interfaces).
# Ported from Sedulous.Fonts (excluding Fonts.Resources).

add_library(RaptorFonts STATIC)
add_library(Raptor::Fonts ALIAS RaptorFonts)

target_link_libraries(RaptorFonts PUBLIC Raptor::Core Raptor::Image Raptor::Policy)

target_sources(RaptorFonts
    PUBLIC
        FILE_SET fonts_modules TYPE CXX_MODULES FILES
            Types.cppm
            Interfaces.cppm
            FontAtlasTexture.cppm
            NullFontService.cppm
            FontsModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
