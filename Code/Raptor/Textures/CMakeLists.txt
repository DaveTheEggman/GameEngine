# Raptor::Textures — `raptor.textures` (texture descriptors + image->RHI format map).
# Ported from Sedulous.Textures (deps: RHI, Images).

add_library(RaptorTextures STATIC)
add_library(Raptor::Textures ALIAS RaptorTextures)

target_link_libraries(RaptorTextures PUBLIC Raptor::Core Raptor::RHI Raptor::Image Raptor::Policy)

target_sources(RaptorTextures
    PUBLIC
        FILE_SET textures_modules TYPE CXX_MODULES FILES
            Types.cppm
            FormatUtils.cppm
            TextureData.cppm
            TexturesModule.cppm)

add_subdirectory(Resource)  # raptor.textures.resource — managed texture resource
add_subdirectory(Importer)  # raptor.textures.importer — image files -> TextureResource

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
