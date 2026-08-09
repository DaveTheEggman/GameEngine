# Editor::Scene - `editor.scene`: the scene editor plugin (SceneEditorPage +
# factory + RegisterSceneEditor). Linked by the editor EXECUTABLE only (never by editor core/app).

add_library(Editor.Scene STATIC)
add_library(Editor::Scene ALIAS Editor.Scene)

target_link_libraries(Editor.Scene
    PUBLIC
        Foundation::Core Foundation::Content Foundation::RHI Foundation::Shell Foundation::Graphics
        Foundation::Runtime Foundation::Runtime.Client Engine::DefaultApp
        Foundation::Scene Engine::Scene Foundation::Scene.Resource
        Foundation::Render Engine::Render Foundation::Animation Engine::Animation
        Foundation::Animation.Resource Pipeline::Animation
        Pipeline::ModelImporter Foundation::Texture.Resource
        Foundation::Particles Foundation::Particles.Resource Pipeline::Particles Engine::Particles
        Foundation::Physics Foundation::Physics.Resource Engine::Physics
        Foundation::UI.Resource Engine::UI Foundation::Audio Foundation::Audio.Resource Engine::Audio
        Foundation::Geometry Foundation::Geometry.Resource Pipeline::Geometry
        Foundation::Materials Foundation::Materials.Resource Pipeline::Materials
        Foundation::UI Foundation::UI.Runtime Foundation::UI.Viewport Foundation::VG.Renderer
        Foundation::Script Foundation::Script.Wren Foundation::Script.Resource Foundation::Script.Facades Engine::Script
        Foundation::Input Foundation::Input.Resource Engine::Input
        Editor::Core Editor::App Foundation::Policy)

target_sources(Editor.Scene
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
            CameraPreview.cppm
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

