# Foundation::Render - `foundation.render`, the renderer (scene-agnostic). Consumes extracted
# render data (ExtractedView) and draws it; knows nothing about the scene. This phase
# is the render-data contract; the GPU forward path lands later (adds the RHI dep).
# Scene integration lives in the Subsystem sub-library (one-way dependency -> here).
add_library(Render STATIC)
add_library(Foundation::Render ALIAS Render)

target_link_libraries(Render
    PUBLIC Foundation::Core Foundation::RHI Foundation::Render.Api Foundation::RenderGraph Foundation::Geometry Foundation::Materials
           Foundation::Shaders Foundation::Shaders.System Foundation::Materials.PipelineCache Foundation::Profiler Foundation::Policy Foundation::Lod)

target_sources(Render
    PRIVATE
        MeshRendererImpl.cpp   # out-of-line MeshRenderer defs (see MeshRenderer.cppm)
        PipelineImpl.cpp        # out-of-line Pipeline defs (see Pipeline.cppm)
        IBLSystemImpl.cpp       # out-of-line IBLSystem defs (see IBLSystem.cppm)
        ReflectionProbeSystemImpl.cpp
        Debug/DebugDrawPassImpl.cpp
        SsrPassImpl.cpp
        AoPassImpl.cpp
        MsaaResolvePassImpl.cpp
        RenderDataImpl.cpp
        ShadowSystemImpl.cpp
        ClusterSystemImpl.cpp
        SpriteRendererImpl.cpp
        DecalPassImpl.cpp
        TaaPassImpl.cpp
        BloomPassImpl.cpp
        SkyPassImpl.cpp
        TonemapPassImpl.cpp
        Debug/DebugDrawImpl.cpp
    PUBLIC
        FILE_SET render_modules TYPE CXX_MODULES FILES
            RenderData.cppm
            Views.cppm
            ExtractContext.cppm
            Resources.cppm
            Pipeline.cppm
            GpuMesh.cppm
            MeshRenderer.cppm
            SpriteRenderer.cppm
            ClusterSystem.cppm
            TonemapPass.cppm
            ShadowSystem.cppm
            IBLSystem.cppm
            ReflectionProbeSystem.cppm
            SkyPass.cppm
            BloomPass.cppm
            TaaPass.cppm
            AoPass.cppm
            MsaaResolvePass.cppm
            SsrPass.cppm
            FxaaPass.cppm
        DebugBlitPass.cppm ExposurePass.cppm
            DecalPass.cppm
            Debug/DebugFont.cppm
            Debug/DebugDraw.cppm
            Debug/DebugDrawPass.cppm
            RenderModule.cppm)

# Scene integration (engine.render): the resource- + scene-aware bridge. Links the base
# Runtime + SceneSubsystem + the cook/material stack, all of which climb, so it climbs too - the
