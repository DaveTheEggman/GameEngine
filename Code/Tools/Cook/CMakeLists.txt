# RaptorCook - the command-line cooker (headless: no shell/GPU).
add_executable(RaptorCook Main.cpp)
target_link_libraries(RaptorCook PRIVATE
    Draconic::Core Draconic::Content Draconic::VFS
    Draconic::Editor Draconic::EditorCore Draconic::EditorCook
    Draconic::TextureEditor Draconic::ImageEditor Draconic::MeshEditor
    Draconic::AnimationEditor Draconic::MaterialEditor Draconic::ShaderEditor
    Draconic::ParticlesEditor Draconic::Input Draconic::InputResource Draconic::InputEditor Draconic::ModelImporter Draconic::ImageResource
    Draconic::Physics Draconic::PhysicsResource Draconic::PhysicsEditor
    Draconic::Audio Draconic::AudioResource Draconic::AudioEditor
    Draconic::Script Draconic::ScriptWren Draconic::ScriptResource Draconic::ScriptFacades Draconic::ScriptEditor
    Draconic::UIResource Draconic::UIEditor)
