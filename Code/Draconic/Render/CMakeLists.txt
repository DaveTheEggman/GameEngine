# Draconic::Render - `draconic.render`, the renderer (scene-agnostic). Consumes extracted
# render data (ExtractedView) and draws it; knows nothing about the scene. This phase
# is the render-data contract; the GPU forward path lands later (adds the RHI dep).
# Scene integration lives in the Subsystem sub-library (one-way dependency -> here).
add_library(DraconicRender STATIC)
add_library(Draconic::Render ALIAS DraconicRender)

target_link_libraries(DraconicRender
    PUBLIC Draconic::Core Draconic::RHI Draconic::RenderApi Draconic::RenderGraph Draconic::Geometry Draconic::Materials
           Draconic::Shaders Draconic::ShaderSystem Draconic::PipelineCache Draconic::Profiler Draconic::Policy)

target_sources(DraconicRender
    PRIVATE
        MeshRendererImpl.cpp   # out-of-line MeshRenderer defs (see MeshRenderer.cppm)
        PipelineImpl.cpp        # out-of-line Pipeline defs (see Pipeline.cppm)
        IBLSystemImpl.cpp       # out-of-line IBLSystem defs (see IBLSystem.cppm)
        ReflectionProbeSystemImpl.cpp
        Debug/DebugDrawPassImpl.cpp
        SsrPassImpl.cpp
        AoPassImpl.cpp
        RenderDataImpl.cpp
    PUBLIC
        FILE_SET render_modules TYPE CXX_MODULES FILES
            RenderData.cppm
            Views.cppm
            ExtractContext.cppm
            Resources.cppm
            Pipeline.cppm
            GpuMesh.cppm
            MeshRenderer.cppm
            MeshShaders.cppm
            SpriteRenderer.cppm
            SpriteShaders.cppm
            ClusterSystem.cppm
            ClusterShaders.cppm
            TonemapPass.cppm
            TonemapShaders.cppm
            ShadowSystem.cppm
            IBLSystem.cppm
            IBLShaders.cppm
            ReflectionProbeSystem.cppm
            ProbeShaders.cppm
            SkyPass.cppm
            SkyShaders.cppm
            BloomPass.cppm
            BloomShaders.cppm
            TaaPass.cppm
            TaaShaders.cppm
            AoPass.cppm
            AoShaders.cppm
            SsrPass.cppm
            SsrShaders.cppm
            FxaaPass.cppm
            FxaaShaders.cppm
            DecalPass.cppm
            DecalShaders.cppm
            Debug/DebugFont.cppm
            Debug/DebugDraw.cppm
            Debug/DebugDrawPass.cppm
            Debug/DebugDrawShaders.cppm
            RenderModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Scene integration: components + extraction + (later) the RenderSubsystem.
add_subdirectory(Subsystem)
