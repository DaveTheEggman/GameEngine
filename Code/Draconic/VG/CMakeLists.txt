# Draconic::VG — `draconic.vg` (2D vector-graphics core: paths, tessellation, context).
# Ported from Sedulous.VG. Pure CPU — emits a VGBatch of vertices/commands that
# draconic.vg.renderer uploads to the GPU. Deps: Core (math/containers) + Image.

add_library(DraconicVG STATIC)
add_library(Draconic::VG ALIAS DraconicVG)

target_link_libraries(DraconicVG PUBLIC Draconic::Core Draconic::Image Draconic::Fonts Draconic::Policy)

target_sources(DraconicVG
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

add_subdirectory(SVG)  # draconic.vg.svg — SVG -> VG (parsers/loader/renderer)
add_subdirectory(Renderer)  # draconic.vg.renderer — VGBatch -> RHI

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
