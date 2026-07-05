# Draconic::VGSVG - `draconic.vg.svg` (SVG -> VG: parsers, loader, renderer).
# Ported from Sedulous.VG.SVG. Standalone parsers (no XML lib); deps VG + Fonts.

add_library(DraconicVGSVG STATIC)
add_library(Draconic::VGSVG ALIAS DraconicVGSVG)

target_link_libraries(DraconicVGSVG PUBLIC Draconic::Core Draconic::VG Draconic::Fonts Draconic::Policy)

target_sources(DraconicVGSVG
    PUBLIC
        FILE_SET vgsvg_modules TYPE CXX_MODULES FILES
            Types.cppm
            Parsers.cppm
            Loader.cppm
            Renderer.cppm
            SVGModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
