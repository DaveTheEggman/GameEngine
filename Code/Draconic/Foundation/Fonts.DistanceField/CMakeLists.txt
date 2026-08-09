# Foundation::Fonts.DistanceField - `draconic.fonts.distancefield` (distance-field font atlas, runtime data type).
# No msdfgen dependency; the import-time baker is in Baker/.

add_library(Fonts.DistanceField STATIC)
add_library(Foundation::Fonts.DistanceField ALIAS Fonts.DistanceField)

target_link_libraries(Fonts.DistanceField PUBLIC Foundation::Core Foundation::Fonts Foundation::Policy)

target_sources(Fonts.DistanceField
    PUBLIC
        FILE_SET fonts_df_modules TYPE CXX_MODULES FILES
            DFFontAtlas.cppm
            FontsDFModule.cppm)
