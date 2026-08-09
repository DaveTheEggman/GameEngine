# Pipeline::UI - `ui.pipeline` (tooling: UI document/theme assets + validating builders + importer).
add_library(UI.Pipeline STATIC)
add_library(Pipeline::UI ALIAS UI.Pipeline)
target_link_libraries(UI.Pipeline PUBLIC
    Foundation::Core Pipeline::Core Pipeline::Importer Foundation::Content Foundation::UI Foundation::UI.Resource Foundation::Policy)
target_sources(UI.Pipeline
    PRIVATE
        UIAssetImpl.cpp
    PUBLIC FILE_SET ui_editor_modules TYPE CXX_MODULES FILES UIAsset.cppm)

