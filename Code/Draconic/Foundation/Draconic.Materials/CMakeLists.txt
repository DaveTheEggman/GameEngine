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

# Render-side PSO cache (needs the ShaderSystem on top of Materials).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Materials.PipelineCache ${CMAKE_CURRENT_BINARY_DIR}/PSO)

# Materials as resources (MaterialSource -> Material; depends on the ShaderResource).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Materials.Resource ${CMAKE_CURRENT_BINARY_DIR}/Resource)

# Material authoring + cook (tooling: MaterialAsset -> MaterialSource).
if(DRACONIC_BUILD_TOOLS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../Editor/Draconic.Materials.Editor ${CMAKE_CURRENT_BINARY_DIR}/Editor)
endif()
