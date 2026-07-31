# Draconic::EditorScene - `draconic.editor.scene`: the scene editor plugin (SceneEditorPage +
# factory + RegisterSceneEditor). Linked by the editor EXECUTABLE only (never by editor core/app).

add_library(Draconic.Editor.Scene STATIC)
add_library(Draconic::EditorScene ALIAS Draconic.Editor.Scene)

target_link_libraries(Draconic.Editor.Scene
    PUBLIC
        Draconic::Core Draconic::Content Draconic::RHI Draconic::Shell Draconic::Graphics
        Draconic::Runtime Draconic::RuntimeClient Draconic::RuntimeDefaultApp
        Draconic::Scene Draconic::SceneSubsystem Draconic::SceneResource Draconic::SceneEditor
        Draconic::Render Draconic::RenderSubsystem Draconic::Animation Draconic::AnimationSubsystem
        Draconic::AnimationResource Draconic::AnimationEditor
        Draconic::ModelImporter Draconic::TextureResource
        Draconic::Particles Draconic::ParticlesResource Draconic::ParticlesEditor Draconic::ParticlesSubsystem
        Draconic::Physics Draconic::PhysicsResource Draconic::PhysicsSubsystem
        Draconic::UIResource Draconic::UISubsystem Draconic::Audio Draconic::AudioResource Draconic::AudioSubsystem
        Draconic::Geometry Draconic::MeshResource Draconic::MeshEditor
        Draconic::Materials Draconic::MaterialResource Draconic::MaterialEditor
        Draconic::UI Draconic::UIRuntime Draconic::UIViewport Draconic::VGRenderer
        Draconic::Script Draconic::ScriptWren Draconic::ScriptResource Draconic::ScriptFacades Draconic::ScriptSubsystem
        Draconic::Input Draconic::InputResource Draconic::InputSubsystem
        Draconic::EditorCore Draconic::EditorApp Draconic::Policy)

target_sources(Draconic.Editor.Scene
    PRIVATE
        EditContextImpl.cpp
        GizmoImpl.cpp
        ComponentGizmosImpl.cpp
        HierarchyViewImpl.cpp
        InspectorViewImpl.cpp
        ScenePageImpl.cpp
        GamePageImpl.cpp
        MaterialPageImpl.cpp
        MeshPageImpl.cpp
        ParticleEffectPageImpl.cpp
        AnimationGraphPageImpl.cpp
        AnimationClipPageImpl.cpp
        SkeletonPageImpl.cpp
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
            MeshPage.cppm
            ParticleEffectPage.cppm
            AnimationGraphPage.cppm
            AnimationClipPage.cppm
            SkeletonPage.cppm
            ModelPrefab.cppm
            EditorSceneModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Editor.Scene.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
