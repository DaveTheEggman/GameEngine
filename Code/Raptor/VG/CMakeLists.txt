# Raptor::VG — `raptor.vg` (2D vector-graphics core: paths, tessellation, context).
# Ported from Sedulous.VG. Pure CPU — emits a VGBatch of vertices/commands that
# raptor.vg.renderer uploads to the GPU. Deps: Core (math/containers) + Image.

add_library(RaptorVG STATIC)
add_library(Raptor::VG ALIAS RaptorVG)

target_link_libraries(RaptorVG PUBLIC Raptor::Core Raptor::Image Raptor::Policy)

target_sources(RaptorVG
    PUBLIC
        FILE_SET vg_modules TYPE CXX_MODULES FILES
            Enums.cppm
            Vertex.cppm
            Fills.cppm
            Style.cppm
            Curves.cppm
            Path.cppm
            Shapes.cppm
            VGModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
