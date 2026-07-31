# Draconic.Tools.Cook - the command-line cooker (headless: no shell/GPU).
add_executable(Draconic.Tools.Cook Main.cpp)
target_link_libraries(Draconic.Tools.Cook PRIVATE
    Draconic::Core Draconic::Content Draconic::VFS
    Draconic::Editor Draconic::EditorCore Draconic::EditorCook
    Draconic::TextureEditor Draconic::ImageEditor Draconic::MeshEditor
    Draconic::AnimationEditor Draconic::MaterialEditor Draconic::ShaderEditor
    Draconic::ParticlesEditor Draconic::Input Draconic::InputResource Draconic::InputEditor Draconic::ModelImporter Draconic::ImageResource
    Draconic::Physics Draconic::PhysicsResource Draconic::PhysicsEditor
    Draconic::Audio Draconic::AudioResource Draconic::AudioEditor
    Draconic::Script Draconic::ScriptWren Draconic::ScriptAngelScript Draconic::ScriptResource Draconic::ScriptFacades
    Draconic::ScriptEditor Draconic::ScriptWrenEditor Draconic::ScriptAngelScriptEditor
    Draconic::UIResource Draconic::UIEditor)

set_target_properties(Draconic.Tools.Cook PROPERTIES OUTPUT_NAME RaptorCook)
