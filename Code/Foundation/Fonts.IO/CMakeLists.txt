# Foundation::Fonts.IO - `foundation.fonts.io` (parser/baker pipeline + FontManager).
# Ported from Sedulous.Fonts.IO (deps: Sedulous.Fonts).

add_library(Fonts.IO STATIC)
add_library(Foundation::Fonts.IO ALIAS Fonts.IO)

target_link_libraries(Fonts.IO PUBLIC Foundation::Core Foundation::Fonts Foundation::Policy)

target_sources(Fonts.IO
    PUBLIC
        FILE_SET fonts_io_modules TYPE CXX_MODULES FILES
            Interfaces.cppm
            Factories.cppm
            FontManager.cppm
            FontsIOModule.cppm)

