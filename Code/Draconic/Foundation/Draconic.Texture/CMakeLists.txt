# Draconic::Texture - `draconic.texture` (texture descriptors + image->RHI format map).
# Ported from Sedulous.Textures (deps: RHI, Images).

add_library(Draconic.Texture STATIC)
add_library(Draconic::Texture ALIAS Draconic.Texture)

target_link_libraries(Draconic.Texture PUBLIC Draconic::Core Draconic::RHI Draconic::Image Draconic::Policy)

target_sources(Draconic.Texture
    PUBLIC
        FILE_SET texture_modules TYPE CXX_MODULES FILES
            Types.cppm
            FormatUtils.cppm
            TextureData.cppm
            TextureModule.cppm)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Texture.Resource ${CMAKE_CURRENT_BINARY_DIR}/Resource)  # draconic.texture.resource - runtime cooked resource + GPU factory
if(DRACONIC_BUILD_TOOLS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../Editor/Draconic.Texture.Editor ${CMAKE_CURRENT_BINARY_DIR}/Editor)    # draconic.texture.editor   - tooling: asset + builder + importer
endif()

