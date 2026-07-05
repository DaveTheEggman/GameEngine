# Draconic::PipelineCache - `draconic.materials.pso` (render-side PSO cache with shader
# version polling). Bridges the ShaderSystem (variants) and Materials (PipelineConfig).
add_library(DraconicPipelineCache STATIC)
add_library(Draconic::PipelineCache ALIAS DraconicPipelineCache)

target_link_libraries(DraconicPipelineCache
    PUBLIC Draconic::Core Draconic::RHI Draconic::Shaders Draconic::ShaderSystem
           Draconic::Materials Draconic::Policy)

target_sources(DraconicPipelineCache
    PUBLIC
        FILE_SET pso_modules TYPE CXX_MODULES FILES
            PipelineStateCache.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
