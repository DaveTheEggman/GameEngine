# Foundation::Texture - `foundation.texture` (texture descriptors + image->RHI format map).
# Ported from Sedulous.Textures (deps: RHI, Images).

util_add_engine_library(Texture ALIAS Foundation::Texture)
target_link_libraries(Texture PUBLIC Foundation::Core Foundation::RHI Foundation::Image Foundation::Policy)

target_sources(Texture
    PRIVATE
        TextureReflectionImpl.cpp
    PUBLIC
        FILE_SET texture_modules TYPE CXX_MODULES FILES
            Types.cppm
            FormatUtils.cppm
            TextureData.cppm
            TextureModule.cppm)

