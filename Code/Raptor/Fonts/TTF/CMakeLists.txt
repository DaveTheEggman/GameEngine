# Raptor::FontsTTF — `raptor.fonts.ttf` (stb_truetype backend + VFS service).
# Ported from Sedulous.Fonts.TTF (deps: Fonts, Fonts.IO, Images, VFS, stb_truetype).

add_library(RaptorFontsTTF STATIC)
add_library(Raptor::FontsTTF ALIAS RaptorFontsTTF)

target_link_libraries(RaptorFontsTTF
    PUBLIC Raptor::Core Raptor::Fonts Raptor::FontsIO Raptor::Image Raptor::VFS Raptor::Policy
    PRIVATE Raptor::Stb)

target_sources(RaptorFontsTTF
    PUBLIC
        FILE_SET fonts_ttf_modules TYPE CXX_MODULES FILES
            Common.cppm
            TrueTypeFont.cppm
            TrueTypeFontAtlas.cppm
            TrueTypeFontParser.cppm
            TrueTypeFontAtlasBaker.cppm
            TrueTypeTextShaper.cppm
            TrueTypeFonts.cppm
            TrueTypeFontService.cppm
            FontsTTFModule.cppm
    PRIVATE
        stb_truetype_impl.cpp)

# stb_truetype.h trips a few warnings the engine treats as errors.
target_compile_options(RaptorFontsTTF PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=missing-field-initializers;-Wno-error=unused-function;-Wno-error=unused-but-set-variable;-Wno-error=sign-compare;-Wno-error=implicit-fallthrough;-Wno-error=type-limits;-Wno-error=cast-qual;-Wno-error=double-promotion>")

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
