# Draconic::RenderApi - `draconic.render.api`: the scene-rendering interface (ISceneRenderer +
# ViewCamera/ViewportRect/CameraOverride/TargetState) in a LIGHT module (core + rhi + scene only),
# so tools drive rendering through the interface without linking the renderer. draconic.render
# re-exports it; RenderSubsystem implements it.

add_library(DraconicRenderApi STATIC)
add_library(Draconic::RenderApi ALIAS DraconicRenderApi)

target_link_libraries(DraconicRenderApi PUBLIC Draconic::Core Draconic::RHI Draconic::Scene Draconic::Policy)

target_sources(DraconicRenderApi
    PUBLIC
        FILE_SET renderapi_modules TYPE CXX_MODULES FILES
            RenderApi.cppm)
