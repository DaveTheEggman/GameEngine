# Foundation::Fonts.TTF - `foundation.fonts.ttf` (stb_truetype backend + VFS service).
# Ported from Sedulous.Fonts.TTF (deps: Fonts, Fonts.IO, Images, VFS, stb_truetype).

util_add_engine_library(Fonts.TTF ALIAS Foundation::Fonts.TTF)
target_link_libraries(Fonts.TTF
    PUBLIC Foundation::Core Foundation::Fonts Foundation::Fonts.IO Foundation::Image Foundation::VFS Foundation::Policy
    PRIVATE ThirdParty::Stb)

target_sources(Fonts.TTF
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
target_compile_options(Fonts.TTF PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=missing-field-initializers;-Wno-error=unused-function;-Wno-error=unused-but-set-variable;-Wno-error=sign-compare;-Wno-error=implicit-fallthrough;-Wno-error=type-limits;-Wno-error=cast-qual;-Wno-error=double-promotion>")

