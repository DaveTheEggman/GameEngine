# Raptor::FontsIO — `raptor.fonts.io` (parser/baker pipeline + FontManager).
# Ported from Sedulous.Fonts.IO (deps: Sedulous.Fonts).

add_library(RaptorFontsIO STATIC)
add_library(Raptor::FontsIO ALIAS RaptorFontsIO)

target_link_libraries(RaptorFontsIO PUBLIC Raptor::Core Raptor::Fonts Raptor::Policy)

target_sources(RaptorFontsIO
    PUBLIC
        FILE_SET fonts_io_modules TYPE CXX_MODULES FILES
            Interfaces.cppm
            Factories.cppm
            FontManager.cppm
            FontsIOModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
