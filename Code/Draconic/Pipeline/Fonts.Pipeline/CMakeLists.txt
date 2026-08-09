# Pipeline::Fonts - `draconic.fonts.editor` (FontAsset + importer + FontAssetBuilder:
# the source/cook tier of the fonts triad). Tooling only; requires the msdfgen DF baker.

add_library(Fonts.Pipeline STATIC)
add_library(Pipeline::Fonts ALIAS Fonts.Pipeline)

target_link_libraries(Fonts.Pipeline
    PUBLIC Foundation::Core Pipeline::Core Editor::Core
           Foundation::Fonts Foundation::Fonts.TTF Foundation::Fonts.IO Foundation::Fonts.Baked
           Foundation::Fonts.Importer Foundation::Fonts.DistanceField
           Foundation::Fonts.DistanceField.Baker Foundation::Fonts.Resource
           Foundation::Content Foundation::Policy)

target_sources(Fonts.Pipeline
    PRIVATE
        FontAssetImpl.cpp
    PUBLIC
        FILE_SET fonts_editor_modules TYPE CXX_MODULES FILES
            FontAsset.cppm)
