# Foundation::Fonts - `foundation.fonts` (font/glyph/text-layout types + interfaces).
# Ported from Sedulous.Fonts (excluding Fonts.Resources).

add_library(Fonts STATIC)
add_library(Foundation::Fonts ALIAS Fonts)

target_link_libraries(Fonts PUBLIC Foundation::Core Foundation::Image Foundation::Policy)

target_sources(Fonts
    PUBLIC
        FILE_SET fonts_modules TYPE CXX_MODULES FILES
            Types.cppm
            Interfaces.cppm
            TextUtil.cppm
            FontAtlasTexture.cppm
            NullFontService.cppm
            ScaledViews.cppm
            FontsModule.cppm)

