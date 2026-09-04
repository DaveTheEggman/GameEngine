# Foundation::VG.Renderer - `foundation.vg.renderer` (VGBatch -> RHI).
# Ported from Sedulous.VG.Renderer. Shader source: shaders/vg.{vert,frag}.hlsl
# (compile via foundation.shaders and pass the modules to VGRenderer::Initialize).

util_add_engine_library(VG.Renderer ALIAS Foundation::VG.Renderer)
target_link_libraries(VG.Renderer PUBLIC
    Foundation::Core Foundation::RHI Foundation::VG Foundation::Image Foundation::Texture Foundation::Policy)

target_sources(VG.Renderer
    PUBLIC
        FILE_SET vgrenderer_modules TYPE CXX_MODULES FILES
            RenderVertex.cppm
            Renderer.cppm
            VGRendererModule.cppm)

