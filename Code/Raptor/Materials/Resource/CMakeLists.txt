# Raptor::MaterialResource — `raptor.materials.resource` (materials as resources:
# MaterialSource -> Material via MaterialFactory; depends on the ShaderResource).
add_library(RaptorMaterialResource STATIC)
add_library(Raptor::MaterialResource ALIAS RaptorMaterialResource)

target_link_libraries(RaptorMaterialResource
    PUBLIC Raptor::Core Raptor::Resource Raptor::Content Raptor::Shaders
           Raptor::ShaderResource Raptor::Materials Raptor::Policy)

target_sources(RaptorMaterialResource
    PUBLIC
        FILE_SET material_resource_modules TYPE CXX_MODULES FILES
            MaterialResource.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
