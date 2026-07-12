# RaptorCook - the command-line cooker (headless: no shell/GPU).
add_executable(RaptorCook Main.cpp)
target_link_libraries(RaptorCook PRIVATE
    Draconic::Core Draconic::Content Draconic::VFS
    Draconic::Editor Draconic::EditorCore Draconic::EditorCook
    Draconic::TextureEditor Draconic::ImageEditor Draconic::MeshEditor
    Draconic::AnimationEditor Draconic::MaterialEditor Draconic::ShaderEditor
    Draconic::ParticlesEditor Draconic::ModelImporter Draconic::ImageResource)
