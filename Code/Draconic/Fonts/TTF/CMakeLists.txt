# Draconic::FontsTTF — `draconic.fonts.ttf` (stb_truetype backend + VFS service).
# Ported from Sedulous.Fonts.TTF (deps: Fonts, Fonts.IO, Images, VFS, stb_truetype).

add_library(DraconicFontsTTF STATIC)
add_library(Draconic::FontsTTF ALIAS DraconicFontsTTF)

target_link_libraries(DraconicFontsTTF
    PUBLIC Draconic::Core Draconic::Fonts Draconic::FontsIO Draconic::Image Draconic::VFS Draconic::Policy
    PRIVATE Draconic::Stb)

target_sources(DraconicFontsTTF
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
target_compile_options(DraconicFontsTTF PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=missing-field-initializers;-Wno-error=unused-function;-Wno-error=unused-but-set-variable;-Wno-error=sign-compare;-Wno-error=implicit-fallthrough;-Wno-error=type-limits;-Wno-error=cast-qual;-Wno-error=double-promotion>")

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
