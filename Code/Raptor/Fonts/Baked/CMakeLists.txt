# Raptor::FontsBaked — `raptor.fonts.baked` (rasterizer-free IFont/IFontAtlas).
# Ported from Sedulous.Fonts.Baked (deps: Sedulous.Fonts).

add_library(RaptorFontsBaked STATIC)
add_library(Raptor::FontsBaked ALIAS RaptorFontsBaked)

target_link_libraries(RaptorFontsBaked PUBLIC Raptor::Core Raptor::Fonts Raptor::Policy)

target_sources(RaptorFontsBaked
    PUBLIC
        FILE_SET fonts_baked_modules TYPE CXX_MODULES FILES
            Baked.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
