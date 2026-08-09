# Draconic::UI.Pipeline - `draconic.ui.editor` (tooling: UI document/theme assets + validating builders + importer).
add_library(Draconic.UI.Pipeline STATIC)
add_library(Draconic::UI.Pipeline ALIAS Draconic.UI.Pipeline)
target_link_libraries(Draconic.UI.Pipeline PUBLIC
    Draconic::Core Draconic::Pipeline.Core Draconic::Editor.Core Draconic::Content Draconic::UI Draconic::UI.Resource Draconic::Policy)
target_sources(Draconic.UI.Pipeline
    PRIVATE
        UIAssetImpl.cpp
    PUBLIC FILE_SET ui_editor_modules TYPE CXX_MODULES FILES UIAsset.cppm)

