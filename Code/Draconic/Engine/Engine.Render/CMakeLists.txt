# Engine::Render - `draconic.engine.render`, the scene side of rendering
# (render components + extraction + later the RenderSubsystem). Depends on Scene AND
# Render (one-way: the renderer never depends back on this or on Scene).
add_library(Engine.Render STATIC)
add_library(Engine::Render ALIAS Engine.Render)

target_link_libraries(Engine.Render
    PUBLIC Foundation::Core Foundation::Scene Engine::Scene Foundation::Render Foundation::Runtime
           Foundation::Geometry Foundation::Materials Foundation::Shaders Foundation::Shaders.System
           Foundation::Texture.Resource Foundation::Resource Foundation::Script.Facades
           Foundation::Materials.PipelineCache Foundation::Profiler Foundation::Policy)

# Dev builds resolve engine built-in shaders straight from the source tree (edit ->
# hot reload); dists stage them as "Shaders" next to the executable (the fallback).
target_compile_definitions(Engine.Render
    PRIVATE BUILTIN_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")

target_sources(Engine.Render
    PRIVATE
        RenderComponentsImpl.cpp   # REFLECT_* bodies (see RenderComponents.cppm)
        RenderSubsystemImpl.cpp    # out-of-line RenderSubsystem defs (see RenderSubsystem.cppm)
        ExtractImpl.cpp            # out-of-line scene-extraction defs (see Extract.cppm)
    PUBLIC
        FILE_SET render_subsystem_modules TYPE CXX_MODULES FILES
            RenderComponents.cppm
            SceneRenderer.cppm
            Extract.cppm
            RenderSubsystem.cppm
            SubsystemModule.cppm)

