# Raptor::PipelineCache — `raptor.materials.pso` (render-side PSO cache with shader
# version polling). Bridges the ShaderSystem (variants) and Materials (PipelineConfig).
add_library(RaptorPipelineCache STATIC)
add_library(Raptor::PipelineCache ALIAS RaptorPipelineCache)

target_link_libraries(RaptorPipelineCache
    PUBLIC Raptor::Core Raptor::RHI Raptor::Shaders Raptor::ShaderSystem
           Raptor::Materials Raptor::Policy)

target_sources(RaptorPipelineCache
    PUBLIC
        FILE_SET pso_modules TYPE CXX_MODULES FILES
            PipelineStateCache.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
