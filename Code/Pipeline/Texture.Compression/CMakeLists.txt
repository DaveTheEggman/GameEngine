# Pipeline::Texture.Compression - `texture.compression`. The ONLY code that includes the block-
# compression encoders (ThirdParty::Bc7enc, PRIVATE so the headers do not leak). Cook/import-time,
# UI-free (the Pipeline rule).
add_library(Texture.Compression STATIC)
add_library(Pipeline::Texture.Compression ALIAS Texture.Compression)

target_link_libraries(Texture.Compression
    PUBLIC Foundation::Core Foundation::RHI
    PRIVATE ThirdParty::Bc7enc)

target_sources(Texture.Compression
    PRIVATE
        TextureCompressionImpl.cpp
    PUBLIC
        FILE_SET texcomp_modules TYPE CXX_MODULES FILES TextureCompression.cppm)
