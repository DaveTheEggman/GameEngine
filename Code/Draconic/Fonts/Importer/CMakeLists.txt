# Draconic::FontsImporter - `draconic.fonts.importer` (editor-time TTF -> baked).
# Ported from Sedulous.Fonts.Importer (deps: Fonts, Fonts.Baked, Fonts.TTF).

add_library(DraconicFontsImporter STATIC)
add_library(Draconic::FontsImporter ALIAS DraconicFontsImporter)

target_link_libraries(DraconicFontsImporter
    PUBLIC Draconic::Core Draconic::Fonts Draconic::FontsBaked Draconic::FontsTTF Draconic::Policy)

target_sources(DraconicFontsImporter
    PUBLIC
        FILE_SET fonts_importer_modules TYPE CXX_MODULES FILES
            FontImporter.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
