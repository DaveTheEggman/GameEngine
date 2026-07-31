# Draconic::VGRenderer - `draconic.vg.renderer` (VGBatch -> RHI).
# Ported from Sedulous.VG.Renderer. Shader source: shaders/vg.{vert,frag}.hlsl
# (compile via draconic.shaders and pass the modules to VGRenderer::Initialize).

add_library(Draconic.VG.Renderer STATIC)
add_library(Draconic::VGRenderer ALIAS Draconic.VG.Renderer)

target_link_libraries(Draconic.VG.Renderer PUBLIC
    Draconic::Core Draconic::RHI Draconic::VG Draconic::Image Draconic::Texture Draconic::Policy)

target_sources(Draconic.VG.Renderer
    PUBLIC
        FILE_SET vgrenderer_modules TYPE CXX_MODULES FILES
            RenderVertex.cppm
            Renderer.cppm
            VGRendererModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.VG.Renderer.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
