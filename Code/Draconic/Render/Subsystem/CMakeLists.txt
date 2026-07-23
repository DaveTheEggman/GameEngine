# Draconic::RenderSubsystem - `draconic.render.subsystem`, the scene side of rendering
# (render components + extraction + later the RenderSubsystem). Depends on Scene AND
# Render (one-way: the renderer never depends back on this or on Scene).
add_library(DraconicRenderSubsystem STATIC)
add_library(Draconic::RenderSubsystem ALIAS DraconicRenderSubsystem)

target_link_libraries(DraconicRenderSubsystem
    PUBLIC Draconic::Core Draconic::Scene Draconic::SceneSubsystem Draconic::Render Draconic::Runtime
           Draconic::Geometry Draconic::Materials Draconic::Shaders Draconic::ShaderSystem
           Draconic::TextureResource Draconic::Resource
           Draconic::PipelineCache Draconic::Profiler Draconic::Policy)

target_sources(DraconicRenderSubsystem
    PRIVATE
        RenderComponentsImpl.cpp   # DRACONIC_REFLECT_* bodies (see RenderComponents.cppm)
        RenderSubsystemImpl.cpp    # out-of-line RenderSubsystem defs (see RenderSubsystem.cppm)
        ExtractImpl.cpp            # out-of-line scene-extraction defs (see Extract.cppm)
    PUBLIC
        FILE_SET render_subsystem_modules TYPE CXX_MODULES FILES
            RenderComponents.cppm
            SceneRenderer.cppm
            Extract.cppm
            RenderSubsystem.cppm
            SubsystemModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
