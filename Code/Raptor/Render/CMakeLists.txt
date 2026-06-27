# Raptor::Render — `raptor.render`, the renderer. This phase is the scene -> render
# extraction bridge (components + ExtractScene); the GPU forward path + RenderSubsystem
# land in later partitions. Depends on Scene, Geometry, Materials (+ RHI once drawing).
add_library(RaptorRender STATIC)
add_library(Raptor::Render ALIAS RaptorRender)

target_link_libraries(RaptorRender
    PUBLIC Raptor::Core Raptor::Scene Raptor::Geometry Raptor::Materials Raptor::Policy)

target_sources(RaptorRender
    PUBLIC
        FILE_SET render_modules TYPE CXX_MODULES FILES
            Components.cppm
            View.cppm
            RenderModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
