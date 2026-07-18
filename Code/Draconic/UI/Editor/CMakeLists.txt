# Draconic::UIEditor - `draconic.ui.editor` (tooling: UI document/theme assets + validating builders + importer).
add_library(DraconicUIEditor STATIC)
add_library(Draconic::UIEditor ALIAS DraconicUIEditor)
target_link_libraries(DraconicUIEditor PUBLIC
    Draconic::Core Draconic::Editor Draconic::EditorCore Draconic::Content Draconic::UI Draconic::UIResource Draconic::Policy)
target_sources(DraconicUIEditor PUBLIC FILE_SET ui_editor_modules TYPE CXX_MODULES FILES UIAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
