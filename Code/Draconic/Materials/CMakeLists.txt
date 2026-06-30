# Draconic::Materials — `draconic.materials`, the data-driven material model
# (PipelineConfig, Material/Builder/Instance, and the bind-group-inferring
# MaterialSystem). Needs the RHI (GPU resources) and Shaders (ShaderFlags).
add_library(DraconicMaterials STATIC)
add_library(Draconic::Materials ALIAS DraconicMaterials)

target_link_libraries(DraconicMaterials
    PUBLIC Draconic::Core Draconic::RHI Draconic::Shaders Draconic::Policy)

target_sources(DraconicMaterials
    PUBLIC
        FILE_SET materials_modules TYPE CXX_MODULES FILES
            Types.cppm
            PipelineConfig.cppm
            Material.cppm
            MaterialBuilder.cppm
            MaterialInstance.cppm
            MaterialSystem.cppm
            MaterialsModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Render-side PSO cache (needs the ShaderSystem on top of Materials).
add_subdirectory(PSO)

# Materials as resources (MaterialSource -> Material; depends on the ShaderResource).
add_subdirectory(Resource)

# Material authoring + cook (tooling: MaterialAsset -> MaterialSource).
add_subdirectory(Editor)
