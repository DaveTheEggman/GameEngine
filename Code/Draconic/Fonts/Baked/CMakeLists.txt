# Draconic::FontsBaked - `draconic.fonts.baked` (rasterizer-free IFont/IFontAtlas).
# Ported from Sedulous.Fonts.Baked (deps: Sedulous.Fonts).

add_library(DraconicFontsBaked STATIC)
add_library(Draconic::FontsBaked ALIAS DraconicFontsBaked)

target_link_libraries(DraconicFontsBaked PUBLIC Draconic::Core Draconic::Fonts Draconic::Policy)

target_sources(DraconicFontsBaked
    PUBLIC
        FILE_SET fonts_baked_modules TYPE CXX_MODULES FILES
            Baked.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
