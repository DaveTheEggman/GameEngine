# Draconic::EditorGameUI - `draconic.editor.gameui`: UI document page (text + live preview).
add_library(DraconicEditorGameUI STATIC)
add_library(Draconic::EditorGameUI ALIAS DraconicEditorGameUI)
target_link_libraries(DraconicEditorGameUI PUBLIC
    Draconic::Core Draconic::Content Draconic::RuntimeClient Draconic::UI
    Draconic::UIResource Draconic::UIEditor Draconic::UISubsystem Draconic::UIRuntime Draconic::UIViewport
    Draconic::Graphics Draconic::RHI Draconic::VGRenderer
    Draconic::EditorCore Draconic::EditorApp Draconic::Policy)
target_sources(DraconicEditorGameUI PUBLIC FILE_SET editor_gameui_modules TYPE CXX_MODULES FILES UIDocumentPage.cppm)
