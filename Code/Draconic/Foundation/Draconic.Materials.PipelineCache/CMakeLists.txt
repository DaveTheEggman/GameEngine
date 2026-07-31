# Draconic::PipelineCache - `draconic.materials.pipelinecache` (render-side PSO cache with shader
# version polling). Bridges the ShaderSystem (variants) and Materials (PipelineConfig).
add_library(Draconic.Materials.PipelineCache STATIC)
add_library(Draconic::PipelineCache ALIAS Draconic.Materials.PipelineCache)

target_link_libraries(Draconic.Materials.PipelineCache
    PUBLIC Draconic::Core Draconic::RHI Draconic::Shaders Draconic::ShaderSystem
           Draconic::Materials Draconic::Policy)

target_sources(Draconic.Materials.PipelineCache
    PUBLIC
        FILE_SET pso_modules TYPE CXX_MODULES FILES
            PipelineStateCache.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
