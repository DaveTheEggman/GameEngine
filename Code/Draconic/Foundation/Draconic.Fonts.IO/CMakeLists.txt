# Draconic::FontsIO - `draconic.fonts.io` (parser/baker pipeline + FontManager).
# Ported from Sedulous.Fonts.IO (deps: Sedulous.Fonts).

add_library(Draconic.Fonts.IO STATIC)
add_library(Draconic::FontsIO ALIAS Draconic.Fonts.IO)

target_link_libraries(Draconic.Fonts.IO PUBLIC Draconic::Core Draconic::Fonts Draconic::Policy)

target_sources(Draconic.Fonts.IO
    PUBLIC
        FILE_SET fonts_io_modules TYPE CXX_MODULES FILES
            Interfaces.cppm
            Factories.cppm
            FontManager.cppm
            FontsIOModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Fonts.IO.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
