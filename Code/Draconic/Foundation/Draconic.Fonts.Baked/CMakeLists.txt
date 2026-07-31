# Draconic::FontsBaked - `draconic.fonts.baked` (rasterizer-free IFont/IFontAtlas).
# Ported from Sedulous.Fonts.Baked (deps: Sedulous.Fonts).

add_library(Draconic.Fonts.Baked STATIC)
add_library(Draconic::FontsBaked ALIAS Draconic.Fonts.Baked)

target_link_libraries(Draconic.Fonts.Baked PUBLIC Draconic::Core Draconic::Fonts Draconic::Policy)

target_sources(Draconic.Fonts.Baked
    PUBLIC
        FILE_SET fonts_baked_modules TYPE CXX_MODULES FILES
            Baked.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Fonts.Baked.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
