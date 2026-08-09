# Foundation::Fonts.Baked - `draconic.fonts.baked` (rasterizer-free IFont/IFontAtlas).
# Ported from Sedulous.Fonts.Baked (deps: Sedulous.Fonts).

add_library(Fonts.Baked STATIC)
add_library(Foundation::Fonts.Baked ALIAS Fonts.Baked)

target_link_libraries(Fonts.Baked PUBLIC Foundation::Core Foundation::Fonts Foundation::Policy)

target_sources(Fonts.Baked
    PUBLIC
        FILE_SET fonts_baked_modules TYPE CXX_MODULES FILES
            Baked.cppm)

