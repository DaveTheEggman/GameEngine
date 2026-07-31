# Draconic::UIEditor - `draconic.ui.editor` (tooling: UI document/theme assets + validating builders + importer).
add_library(Draconic.UI.Editor STATIC)
add_library(Draconic::UIEditor ALIAS Draconic.UI.Editor)
target_link_libraries(Draconic.UI.Editor PUBLIC
    Draconic::Core Draconic::Editor Draconic::EditorCore Draconic::Content Draconic::UI Draconic::UIResource Draconic::Policy)
target_sources(Draconic.UI.Editor PUBLIC FILE_SET ui_editor_modules TYPE CXX_MODULES FILES UIAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.UI.Editor.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
