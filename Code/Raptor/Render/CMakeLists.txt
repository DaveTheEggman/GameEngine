# Raptor::Render — `raptor.render`, the renderer (scene-agnostic). Consumes extracted
# render data (ExtractedView) and draws it; knows nothing about the scene. This phase
# is the render-data contract; the GPU forward path lands later (adds the RHI dep).
# Scene integration lives in the Subsystem sub-library (one-way dependency -> here).
add_library(RaptorRender STATIC)
add_library(Raptor::Render ALIAS RaptorRender)

target_link_libraries(RaptorRender
    PUBLIC Raptor::Core Raptor::RHI Raptor::Geometry Raptor::Materials
           Raptor::Shaders Raptor::ShaderSystem Raptor::PipelineCache Raptor::Policy)

target_sources(RaptorRender
    PUBLIC
        FILE_SET render_modules TYPE CXX_MODULES FILES
            RenderData.cppm
            Views.cppm
            Pipeline.cppm
            MeshGpu.cppm
            MeshRenderer.cppm
            RenderModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Scene integration: components + extraction + (later) the RenderSubsystem.
add_subdirectory(Subsystem)
