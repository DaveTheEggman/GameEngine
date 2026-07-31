# Draconic::VG.SVG - `draconic.vg.svg` (SVG -> VG: parsers, loader, renderer).
# Ported from Sedulous.VG.SVG. Standalone parsers (no XML lib); deps VG + Fonts.

add_library(Draconic.VG.SVG STATIC)
add_library(Draconic::VG.SVG ALIAS Draconic.VG.SVG)

target_link_libraries(Draconic.VG.SVG PUBLIC Draconic::Core Draconic::VG Draconic::Fonts Draconic::Policy)

target_sources(Draconic.VG.SVG
    PUBLIC
        FILE_SET vgsvg_modules TYPE CXX_MODULES FILES
            Types.cppm
            Parsers.cppm
            Loader.cppm
            Renderer.cppm
            SVGModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.VG.SVG.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
