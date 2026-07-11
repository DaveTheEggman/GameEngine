# Draconic::EditorScene - `draconic.editor.scene`: the scene editor plugin (SceneEditorPage +
# factory + RegisterSceneEditor). Linked by the editor EXECUTABLE only (never by editor core/app).

add_library(DraconicEditorScene STATIC)
add_library(Draconic::EditorScene ALIAS DraconicEditorScene)

target_link_libraries(DraconicEditorScene
    PUBLIC
        Draconic::Core Draconic::Content Draconic::RHI Draconic::Shell Draconic::Graphics
        Draconic::Runtime Draconic::RuntimeClient
        Draconic::Scene Draconic::SceneSubsystem Draconic::SceneResource Draconic::SceneEditor
        Draconic::Render Draconic::RenderSubsystem
        Draconic::UI Draconic::UIRuntime Draconic::UIViewport Draconic::VGRenderer
        Draconic::EditorCore Draconic::EditorApp Draconic::Policy)

target_sources(DraconicEditorScene
    PUBLIC
        FILE_SET editorscene_modules TYPE CXX_MODULES FILES
            EditorCamera.cppm
            EditContext.cppm
            HierarchyView.cppm
            InspectorView.cppm
            ScenePage.cppm
            EditorSceneModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
