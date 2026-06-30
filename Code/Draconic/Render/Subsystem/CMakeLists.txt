# Draconic::RenderSubsystem — `draconic.render.subsystem`, the scene side of rendering
# (render components + extraction + later the RenderSubsystem). Depends on Scene AND
# Render (one-way: the renderer never depends back on this or on Scene).
add_library(DraconicRenderSubsystem STATIC)
add_library(Draconic::RenderSubsystem ALIAS DraconicRenderSubsystem)

target_link_libraries(DraconicRenderSubsystem
    PUBLIC Draconic::Core Draconic::Scene Draconic::SceneSubsystem Draconic::Render Draconic::Runtime
           Draconic::Geometry Draconic::Materials Draconic::Shaders Draconic::ShaderSystem
           Draconic::PipelineCache Draconic::Profiler Draconic::Policy)

target_sources(DraconicRenderSubsystem
    PUBLIC
        FILE_SET render_subsystem_modules TYPE CXX_MODULES FILES
            Components.cppm
            SceneRenderer.cppm
            Extract.cppm
            RenderSubsystem.cppm
            SubsystemModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
