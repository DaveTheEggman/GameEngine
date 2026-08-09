# Foundation::Materials.PipelineCache - `draconic.materials.pipelinecache` (render-side PSO cache with shader
# version polling). Bridges the ShaderSystem (variants) and Materials (PipelineConfig).
add_library(Materials.PipelineCache STATIC)
add_library(Foundation::Materials.PipelineCache ALIAS Materials.PipelineCache)

target_link_libraries(Materials.PipelineCache
    PUBLIC Foundation::Core Foundation::RHI Foundation::Shaders Foundation::Shaders.System
           Foundation::Materials Foundation::Policy)

target_sources(Materials.PipelineCache
    PUBLIC
        FILE_SET pso_modules TYPE CXX_MODULES FILES
            PipelineStateCache.cppm)

