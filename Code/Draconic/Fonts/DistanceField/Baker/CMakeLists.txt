# Draconic::FontsDFBaker - `draconic.fonts.df.baker` (import-time MSDF atlas generation).
# Links msdfgen; not needed at runtime.

add_library(DraconicFontsDFBaker STATIC)
add_library(Draconic::FontsDFBaker ALIAS DraconicFontsDFBaker)

target_link_libraries(DraconicFontsDFBaker
    PUBLIC  Draconic::Core Draconic::Fonts Draconic::FontsDF Draconic::FontsIO Draconic::FontsTTF Draconic::Policy
    PRIVATE Draconic::MsdfgenCore Draconic::Stb)

# msdfgen_impl.cpp includes msdfgen headers which may use exceptions.
# Suppress -Werror for this TU so msdfgen warnings don't break the build.
target_sources(DraconicFontsDFBaker
    PUBLIC
        FILE_SET fonts_df_baker_modules TYPE CXX_MODULES FILES
            DFFontAtlasBaker.cppm
            DFFonts.cppm
            FontsDFBakerModule.cppm
    PRIVATE
        msdfgen_impl.cpp)

set_source_files_properties(msdfgen_impl.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error>")

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
