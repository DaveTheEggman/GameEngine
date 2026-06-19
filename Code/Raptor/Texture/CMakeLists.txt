# Raptor::Texture — `raptor.texture` (texture descriptors + image->RHI format map).
# Ported from Sedulous.Textures (deps: RHI, Images).

add_library(RaptorTexture STATIC)
add_library(Raptor::Texture ALIAS RaptorTexture)

target_link_libraries(RaptorTexture PUBLIC Raptor::Core Raptor::RHI Raptor::Image Raptor::Policy)

target_sources(RaptorTexture
    PUBLIC
        FILE_SET texture_modules TYPE CXX_MODULES FILES
            Types.cppm
            FormatUtils.cppm
            TextureData.cppm
            TextureModule.cppm)

add_subdirectory(Resource)  # raptor.texture.resource — runtime cooked resource + GPU factory
add_subdirectory(Editor)    # raptor.texture.editor   — tooling: asset + builder + importer

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
