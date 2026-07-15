# RaptorEditor - the Draconic editor executable (assembly of draconic.editor.app + the
# per-subsystem editor plugin modules; see docs/design/editor.md §3.1).
add_executable(RaptorEditor Main.cpp)
target_link_libraries(RaptorEditor PRIVATE
    Draconic::Core
    Draconic::Runtime Draconic::RuntimeClient Draconic::RuntimeDesktop Draconic::ShellDesktop
    Draconic::Shell Draconic::Graphics Draconic::GraphicsGpu
    Draconic::SceneSubsystem Draconic::RenderSubsystem Draconic::AnimationSubsystem Draconic::ParticlesSubsystem
    Draconic::EditorCore Draconic::EditorApp Draconic::EditorScene
    Draconic::Editor Draconic::EditorCook
    Draconic::Resource Draconic::MeshResource Draconic::MaterialResource Draconic::TextureResource
    Draconic::TextureEditor Draconic::ImageEditor Draconic::MeshEditor
    Draconic::AnimationEditor Draconic::MaterialEditor Draconic::ShaderEditor
    Draconic::ParticlesEditor Draconic::ModelImporter Draconic::ImageResource
    Draconic::AnimationResource)
target_compile_definitions(RaptorEditor PRIVATE
    DRACONIC_EDITOR_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf")
# Copy SDL3.dll next to the executable on Windows (same as the samples' helper, which is
# defined in Code/Samples/ and not visible here).
draconic_copy_runtime_deps(RaptorEditor EXTRA ${DRACONIC_SDL3_DLL})
