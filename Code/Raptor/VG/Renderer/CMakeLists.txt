# Raptor::VGRenderer — `raptor.vg.renderer` (VGBatch -> RHI).
# Ported from Sedulous.VG.Renderer. Shader source: shaders/vg.{vert,frag}.hlsl
# (compile via raptor.shaders and pass the modules to VGRenderer::Initialize).

add_library(RaptorVGRenderer STATIC)
add_library(Raptor::VGRenderer ALIAS RaptorVGRenderer)

target_link_libraries(RaptorVGRenderer PUBLIC
    Raptor::Core Raptor::RHI Raptor::VG Raptor::Image Raptor::Texture Raptor::Policy)

target_sources(RaptorVGRenderer
    PUBLIC
        FILE_SET vgrenderer_modules TYPE CXX_MODULES FILES
            RenderVertex.cppm
            Renderer.cppm
            VGRendererModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
