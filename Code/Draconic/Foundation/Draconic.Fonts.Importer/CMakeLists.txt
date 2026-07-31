# Draconic::FontsImporter - `draconic.fonts.importer` (editor-time TTF -> baked).
# Ported from Sedulous.Fonts.Importer (deps: Fonts, Fonts.Baked, Fonts.TTF).

add_library(Draconic.Fonts.Importer STATIC)
add_library(Draconic::FontsImporter ALIAS Draconic.Fonts.Importer)

target_link_libraries(Draconic.Fonts.Importer
    PUBLIC Draconic::Core Draconic::Fonts Draconic::FontsBaked Draconic::FontsTTF Draconic::Policy)

target_sources(Draconic.Fonts.Importer
    PUBLIC
        FILE_SET fonts_importer_modules TYPE CXX_MODULES FILES
            FontImporter.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Fonts.Importer.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
