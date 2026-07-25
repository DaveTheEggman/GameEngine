# Draconic::FontsDF - `draconic.fonts.df` (distance-field font atlas, runtime data type).
# No msdfgen dependency; the import-time baker is in Baker/.

add_library(DraconicFontsDF STATIC)
add_library(Draconic::FontsDF ALIAS DraconicFontsDF)

target_link_libraries(DraconicFontsDF PUBLIC Draconic::Core Draconic::Fonts Draconic::Policy)

target_sources(DraconicFontsDF
    PUBLIC
        FILE_SET fonts_df_modules TYPE CXX_MODULES FILES
            DFFontAtlas.cppm
            FontsDFModule.cppm)

if(TARGET Draconic::MsdfgenCore)
    add_subdirectory(Baker)
endif()

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
