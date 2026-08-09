# Foundation::VG.SVG - `draconic.vg.svg` (SVG -> VG: parsers, loader, renderer).
# Ported from Sedulous.VG.SVG. Standalone parsers (no XML lib); deps VG + Fonts.

add_library(VG.SVG STATIC)
add_library(Foundation::VG.SVG ALIAS VG.SVG)

target_link_libraries(VG.SVG PUBLIC Foundation::Core Foundation::VG Foundation::Fonts Foundation::Policy)

target_sources(VG.SVG
    PUBLIC
        FILE_SET vgsvg_modules TYPE CXX_MODULES FILES
            Types.cppm
            Parsers.cppm
            Loader.cppm
            Renderer.cppm
            SVGModule.cppm)

