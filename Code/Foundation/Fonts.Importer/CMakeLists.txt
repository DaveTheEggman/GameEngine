# Foundation::Fonts.Importer - `foundation.fonts.importer` (editor-time TTF -> baked).
# Ported from Sedulous.Fonts.Importer (deps: Fonts, Fonts.Baked, Fonts.TTF).

util_add_engine_library(Fonts.Importer ALIAS Foundation::Fonts.Importer)
target_link_libraries(Fonts.Importer
    PUBLIC Foundation::Core Foundation::Fonts Foundation::Fonts.Baked Foundation::Fonts.TTF Foundation::Policy)

target_sources(Fonts.Importer
    PUBLIC
        FILE_SET fonts_importer_modules TYPE CXX_MODULES FILES
            FontImporter.cppm)

