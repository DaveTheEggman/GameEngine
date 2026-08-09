# Draconic::Fonts.Pipeline - `draconic.fonts.editor` (FontAsset + importer + FontAssetBuilder:
# the source/cook tier of the fonts triad). Tooling only; requires the msdfgen DF baker.

add_library(Draconic.Fonts.Pipeline STATIC)
add_library(Draconic::Fonts.Pipeline ALIAS Draconic.Fonts.Pipeline)

target_link_libraries(Draconic.Fonts.Pipeline
    PUBLIC Draconic::Core Draconic::Pipeline.Core Draconic::Editor.Core
           Draconic::Fonts Draconic::Fonts.TTF Draconic::Fonts.IO Draconic::Fonts.Baked
           Draconic::Fonts.Importer Draconic::Fonts.DistanceField
           Draconic::Fonts.DistanceField.Baker Draconic::Fonts.Resource
           Draconic::Content Draconic::Policy)

target_sources(Draconic.Fonts.Pipeline
    PRIVATE
        FontAssetImpl.cpp
    PUBLIC
        FILE_SET fonts_editor_modules TYPE CXX_MODULES FILES
            FontAsset.cppm)
