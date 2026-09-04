# Pipeline::UI - `ui.pipeline` (tooling: UI document/theme assets + validating builders + importer).
util_add_engine_library(UI.Pipeline ALIAS Pipeline::UI)
target_link_libraries(UI.Pipeline PUBLIC
    Foundation::Core Pipeline::Core Pipeline::Importer Foundation::Content Foundation::UI Foundation::UI.Gamekit Foundation::UI.Resource Foundation::Policy)
target_sources(UI.Pipeline
    PRIVATE
        UIAssetImpl.cpp
    PUBLIC FILE_SET ui_editor_modules TYPE CXX_MODULES FILES UIAsset.cppm)

