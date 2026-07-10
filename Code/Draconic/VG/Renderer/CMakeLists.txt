# Draconic::VGRenderer - `draconic.vg.renderer` (VGBatch -> RHI).
# Ported from Sedulous.VG.Renderer. Shader source: shaders/vg.{vert,frag}.hlsl
# (compile via draconic.shaders and pass the modules to VGRenderer::Initialize).

add_library(DraconicVGRenderer STATIC)
add_library(Draconic::VGRenderer ALIAS DraconicVGRenderer)

target_link_libraries(DraconicVGRenderer PUBLIC
    Draconic::Core Draconic::RHI Draconic::VG Draconic::Image Draconic::Texture Draconic::Policy)

target_sources(DraconicVGRenderer
    PUBLIC
        FILE_SET vgrenderer_modules TYPE CXX_MODULES FILES
            RenderVertex.cppm
            Renderer.cppm
            Shaders.cppm
            VGRendererModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
