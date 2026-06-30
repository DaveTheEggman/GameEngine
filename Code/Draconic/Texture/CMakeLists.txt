# Draconic::Texture — `draconic.texture` (texture descriptors + image->RHI format map).
# Ported from Sedulous.Textures (deps: RHI, Images).

add_library(DraconicTexture STATIC)
add_library(Draconic::Texture ALIAS DraconicTexture)

target_link_libraries(DraconicTexture PUBLIC Draconic::Core Draconic::RHI Draconic::Image Draconic::Policy)

target_sources(DraconicTexture
    PUBLIC
        FILE_SET texture_modules TYPE CXX_MODULES FILES
            Types.cppm
            FormatUtils.cppm
            TextureData.cppm
            TextureModule.cppm)

add_subdirectory(Resource)  # draconic.texture.resource — runtime cooked resource + GPU factory
add_subdirectory(Editor)    # draconic.texture.editor   — tooling: asset + builder + importer

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
