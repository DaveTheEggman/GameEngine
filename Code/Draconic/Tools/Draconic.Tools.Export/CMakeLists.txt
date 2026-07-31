# Draconic.Tools.Export - packages a project into a shippable dist (cook -> stage scenes -> Content.pak
# + player.xml + Draconic.Engine.Player). See docs/design/roadmap.md "Milestone: MVP-to-Export".
add_executable(Draconic.Tools.Export Main.cpp)
add_dependencies(Draconic.Tools.Export Draconic.Engine.Player)   # stages the player exe when present
target_link_libraries(Draconic.Tools.Export PRIVATE
    Draconic::Core Draconic::SceneResource
    Draconic::Editor Draconic::EditorCook Draconic::EditorCore
    Draconic::TextureEditor Draconic::ImageEditor Draconic::ImageResource Draconic::MeshEditor
    Draconic::AnimationEditor Draconic::MaterialEditor Draconic::ShaderEditor
    Draconic::ParticlesEditor Draconic::Input Draconic::InputResource Draconic::InputEditor Draconic::ModelImporter
    Draconic::RenderSubsystem Draconic::AnimationSubsystem Draconic::ParticlesSubsystem
    Draconic::Physics Draconic::PhysicsResource Draconic::PhysicsEditor Draconic::PhysicsSubsystem
    Draconic::Audio Draconic::AudioResource Draconic::AudioEditor
    Draconic::Script Draconic::ScriptWren Draconic::ScriptAngelScript Draconic::ScriptResource Draconic::ScriptFacades
    Draconic::ScriptEditor Draconic::ScriptWrenEditor Draconic::ScriptAngelScriptEditor
    Draconic::UIResource Draconic::UIEditor)

set_target_properties(Draconic.Tools.Export PROPERTIES OUTPUT_NAME RaptorExport)
