# Draconic::FontsIO — `draconic.fonts.io` (parser/baker pipeline + FontManager).
# Ported from Sedulous.Fonts.IO (deps: Sedulous.Fonts).

add_library(DraconicFontsIO STATIC)
add_library(Draconic::FontsIO ALIAS DraconicFontsIO)

target_link_libraries(DraconicFontsIO PUBLIC Draconic::Core Draconic::Fonts Draconic::Policy)

target_sources(DraconicFontsIO
    PUBLIC
        FILE_SET fonts_io_modules TYPE CXX_MODULES FILES
            Interfaces.cppm
            Factories.cppm
            FontManager.cppm
            FontsIOModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
