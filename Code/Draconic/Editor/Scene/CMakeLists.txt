# Draconic::EditorScene - `draconic.editor.scene`: the scene editor plugin (SceneEditorPage +
# factory + RegisterSceneEditor). Linked by the editor EXECUTABLE only (never by editor core/app).

add_library(DraconicEditorScene STATIC)
add_library(Draconic::EditorScene ALIAS DraconicEditorScene)

target_link_libraries(DraconicEditorScene
    PUBLIC
        Draconic::Core Draconic::Content Draconic::RHI Draconic::Shell Draconic::Graphics
        Draconic::Runtime Draconic::RuntimeClient Draconic::RuntimeDefaultApp
        Draconic::Scene Draconic::SceneSubsystem Draconic::SceneResource Draconic::SceneEditor
        Draconic::Render Draconic::RenderSubsystem Draconic::Animation Draconic::AnimationSubsystem
        Draconic::ModelImporter Draconic::TextureResource Draconic::ParticlesResource
        Draconic::Physics Draconic::PhysicsResource Draconic::PhysicsSubsystem
        Draconic::UIResource Draconic::UISubsystem Draconic::Audio Draconic::AudioResource Draconic::AudioSubsystem
        Draconic::Geometry Draconic::Materials Draconic::MaterialResource Draconic::MaterialEditor
        Draconic::UI Draconic::UIRuntime Draconic::UIViewport Draconic::VGRenderer
        Draconic::Script Draconic::ScriptWren
        Draconic::Input Draconic::InputResource Draconic::InputSubsystem
        Draconic::EditorCore Draconic::EditorApp Draconic::Policy)

target_sources(DraconicEditorScene
    PUBLIC
        FILE_SET editorscene_modules TYPE CXX_MODULES FILES
            EditorCamera.cppm
            EditContext.cppm
            Gizmo.cppm
            ComponentGizmos.cppm
            HierarchyView.cppm
            InspectorView.cppm
            ScenePage.cppm
            GamePage.cppm
            MaterialPage.cppm
            ModelPrefab.cppm
            EditorSceneModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
