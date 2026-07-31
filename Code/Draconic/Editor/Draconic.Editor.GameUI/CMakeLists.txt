# Draconic::EditorGameUI - `draconic.editor.gameui`: UI document page (text + live preview).
add_library(Draconic.Editor.GameUI STATIC)
add_library(Draconic::EditorGameUI ALIAS Draconic.Editor.GameUI)
target_link_libraries(Draconic.Editor.GameUI PUBLIC
    Draconic::Core Draconic::Content Draconic::RuntimeClient Draconic::UI Draconic::UIToolkit Draconic::Xml
    Draconic::UIResource Draconic::UIEditor Draconic::UISubsystem Draconic::UIRuntime Draconic::UIViewport
    Draconic::Graphics Draconic::RHI Draconic::VGRenderer
    Draconic::EditorCore Draconic::EditorApp Draconic::Policy)
target_sources(Draconic.Editor.GameUI PRIVATE UIDocumentPageImpl.cpp PUBLIC FILE_SET editor_gameui_modules TYPE CXX_MODULES FILES UIDocumentPage.cppm)
