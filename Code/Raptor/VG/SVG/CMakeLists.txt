# Raptor::VGSVG — `raptor.vg.svg` (SVG -> VG: parsers, loader, renderer).
# Ported from Sedulous.VG.SVG. Standalone parsers (no XML lib); deps VG + Fonts.

add_library(RaptorVGSVG STATIC)
add_library(Raptor::VGSVG ALIAS RaptorVGSVG)

target_link_libraries(RaptorVGSVG PUBLIC Raptor::Core Raptor::VG Raptor::Fonts Raptor::Policy)

target_sources(RaptorVGSVG
    PUBLIC
        FILE_SET vgsvg_modules TYPE CXX_MODULES FILES
            Types.cppm
            Parsers.cppm
            Loader.cppm
            Renderer.cppm
            SVGModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
