# Raptor::Materials — `raptor.materials`, the data-driven material model
# (PipelineConfig, Material/Builder/Instance, and the bind-group-inferring
# MaterialSystem). Needs the RHI (GPU resources) and Shaders (ShaderFlags).
add_library(RaptorMaterials STATIC)
add_library(Raptor::Materials ALIAS RaptorMaterials)

target_link_libraries(RaptorMaterials
    PUBLIC Raptor::Core Raptor::RHI Raptor::Shaders Raptor::Policy)

target_sources(RaptorMaterials
    PUBLIC
        FILE_SET materials_modules TYPE CXX_MODULES FILES
            Types.cppm
            PipelineConfig.cppm
            Material.cppm
            MaterialBuilder.cppm
            MaterialInstance.cppm
            MaterialSystem.cppm
            MaterialsModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
