# Draconic::VG - `draconic.vg` (2D vector-graphics core: paths, tessellation, context).
# Ported from Sedulous.VG. Pure CPU - emits a VGBatch of vertices/commands that
# draconic.vg.renderer uploads to the GPU. Deps: Core (math/containers) + Image.

add_library(Draconic.VG STATIC)
add_library(Draconic::VG ALIAS Draconic.VG)

target_link_libraries(Draconic.VG PUBLIC Draconic::Core Draconic::Image Draconic::Fonts Draconic::Policy)

target_sources(Draconic.VG
    PUBLIC
        FILE_SET vg_modules TYPE CXX_MODULES FILES
            Enums.cppm
            Vertex.cppm
            Fills.cppm
            Style.cppm
            Curves.cppm
            Path.cppm
            Shapes.cppm
            Tessellation.cppm
            Batch.cppm
            Cache.cppm
            Context.cppm
            VGModule.cppm)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.VG.SVG ${CMAKE_CURRENT_BINARY_DIR}/SVG)  # draconic.vg.svg - SVG -> VG (parsers/loader/renderer)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.VG.Renderer ${CMAKE_CURRENT_BINARY_DIR}/Renderer)  # draconic.vg.renderer - VGBatch -> RHI

