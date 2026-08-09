# Foundation::VG.Renderer - `foundation.vg.renderer` (VGBatch -> RHI).
# Ported from Sedulous.VG.Renderer. Shader source: shaders/vg.{vert,frag}.hlsl
# (compile via foundation.shaders and pass the modules to VGRenderer::Initialize).

add_library(VG.Renderer STATIC)
add_library(Foundation::VG.Renderer ALIAS VG.Renderer)

target_link_libraries(VG.Renderer PUBLIC
    Foundation::Core Foundation::RHI Foundation::VG Foundation::Image Foundation::Texture Foundation::Policy)

target_sources(VG.Renderer
    PUBLIC
        FILE_SET vgrenderer_modules TYPE CXX_MODULES FILES
            RenderVertex.cppm
            Renderer.cppm
            VGRendererModule.cppm)

