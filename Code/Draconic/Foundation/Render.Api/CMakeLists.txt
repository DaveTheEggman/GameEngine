# Foundation::Render.Api - `draconic.render.api`: the scene-rendering interface (ISceneRenderer +
# ViewCamera/ViewportRect/CameraOverride/TargetState) in a LIGHT module (core + rhi + scene only),
# so tools drive rendering through the interface without linking the renderer. draconic.render
# re-exports it; RenderSubsystem implements it.

add_library(Render.Api STATIC)
add_library(Foundation::Render.Api ALIAS Render.Api)

target_link_libraries(Render.Api PUBLIC Foundation::Core Foundation::RHI Foundation::Scene Foundation::Policy)

target_sources(Render.Api
    PUBLIC
        FILE_SET renderapi_modules TYPE CXX_MODULES FILES
            RenderApi.cppm)
