# Foundation::Fonts.DistanceField.Baker - `foundation.fonts.distancefield.baker` (import-time MSDF atlas generation).
# Links msdfgen; not needed at runtime.

util_add_engine_library(Fonts.DistanceField.Baker ALIAS Foundation::Fonts.DistanceField.Baker)
target_link_libraries(Fonts.DistanceField.Baker
    PUBLIC  Foundation::Core Foundation::Fonts Foundation::Fonts.DistanceField Foundation::Fonts.IO Foundation::Fonts.TTF Foundation::Policy
    PRIVATE ThirdParty::MsdfgenCore ThirdParty::Stb)

# msdfgen_impl.cpp includes msdfgen headers which may use exceptions.
# Suppress -Werror for this TU so msdfgen warnings don't break the build.
target_sources(Fonts.DistanceField.Baker
    PUBLIC
        FILE_SET fonts_df_baker_modules TYPE CXX_MODULES FILES
            DFFontAtlasBaker.cppm
            DFFonts.cppm
            FontsDFBakerModule.cppm
    PRIVATE
        msdfgen_impl.cpp)

set_source_files_properties(msdfgen_impl.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error>")

