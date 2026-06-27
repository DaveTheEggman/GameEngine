# Raptor::RenderSubsystem — `raptor.render.subsystem`, the scene side of rendering
# (render components + extraction + later the RenderSubsystem). Depends on Scene AND
# Render (one-way: the renderer never depends back on this or on Scene).
add_library(RaptorRenderSubsystem STATIC)
add_library(Raptor::RenderSubsystem ALIAS RaptorRenderSubsystem)

target_link_libraries(RaptorRenderSubsystem
    PUBLIC Raptor::Core Raptor::Scene Raptor::Render Raptor::Geometry Raptor::Materials Raptor::Policy)

target_sources(RaptorRenderSubsystem
    PUBLIC
        FILE_SET render_subsystem_modules TYPE CXX_MODULES FILES
            Components.cppm
            Extract.cppm
            SubsystemModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
