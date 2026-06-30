# Draconic::Render — `draconic.render`, the renderer (scene-agnostic). Consumes extracted
# render data (ExtractedView) and draws it; knows nothing about the scene. This phase
# is the render-data contract; the GPU forward path lands later (adds the RHI dep).
# Scene integration lives in the Subsystem sub-library (one-way dependency -> here).
add_library(DraconicRender STATIC)
add_library(Draconic::Render ALIAS DraconicRender)

target_link_libraries(DraconicRender
    PUBLIC Draconic::Core Draconic::RHI Draconic::RenderGraph Draconic::Geometry Draconic::Materials
           Draconic::Shaders Draconic::ShaderSystem Draconic::PipelineCache Draconic::Profiler Draconic::Policy)

target_sources(DraconicRender
    PUBLIC
        FILE_SET render_modules TYPE CXX_MODULES FILES
            RenderData.cppm
            Views.cppm
            ExtractContext.cppm
            Resources.cppm
            Pipeline.cppm
            GpuMesh.cppm
            MeshRenderer.cppm
            ClusterSystem.cppm
            TonemapPass.cppm
            ShadowSystem.cppm
            RenderModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Scene integration: components + extraction + (later) the RenderSubsystem.
add_subdirectory(Subsystem)
