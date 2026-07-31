# Draconic::Materials - `draconic.materials`, the data-driven material model
# (PipelineConfig, Material/Builder/Instance, and the bind-group-inferring
# MaterialSystem). Needs the RHI (GPU resources) and Shaders (ShaderFlags).
add_library(Draconic.Materials STATIC)
add_library(Draconic::Materials ALIAS Draconic.Materials)

target_link_libraries(Draconic.Materials
    PUBLIC Draconic::Core Draconic::RHI Draconic::Shaders Draconic::Policy)

target_sources(Draconic.Materials
    PUBLIC
        FILE_SET materials_modules TYPE CXX_MODULES FILES
            Types.cppm
            PipelineConfig.cppm
            Material.cppm
            MaterialBuilder.cppm
            MaterialInstance.cppm
            MaterialSystem.cppm
            MaterialsModule.cppm)

