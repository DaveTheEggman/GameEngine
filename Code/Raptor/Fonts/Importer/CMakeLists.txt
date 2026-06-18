# Raptor::FontsImporter — `raptor.fonts.importer` (editor-time TTF -> baked).
# Ported from Sedulous.Fonts.Importer (deps: Fonts, Fonts.Baked, Fonts.TTF).

add_library(RaptorFontsImporter STATIC)
add_library(Raptor::FontsImporter ALIAS RaptorFontsImporter)

target_link_libraries(RaptorFontsImporter
    PUBLIC Raptor::Core Raptor::Fonts Raptor::FontsBaked Raptor::FontsTTF Raptor::Policy)

target_sources(RaptorFontsImporter
    PUBLIC
        FILE_SET fonts_importer_modules TYPE CXX_MODULES FILES
            FontImporter.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
