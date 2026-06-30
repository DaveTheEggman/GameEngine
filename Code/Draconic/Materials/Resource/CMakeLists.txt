# Draconic::MaterialResource — `draconic.materials.resource` (materials as resources:
# MaterialSource -> Material via MaterialFactory; depends on the ShaderResource).
add_library(DraconicMaterialResource STATIC)
add_library(Draconic::MaterialResource ALIAS DraconicMaterialResource)

target_link_libraries(DraconicMaterialResource
    PUBLIC Draconic::Core Draconic::Resource Draconic::Content Draconic::Shaders
           Draconic::ShaderResource Draconic::Materials Draconic::Policy)

target_sources(DraconicMaterialResource
    PUBLIC
        FILE_SET material_resource_modules TYPE CXX_MODULES FILES
            MaterialResource.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
