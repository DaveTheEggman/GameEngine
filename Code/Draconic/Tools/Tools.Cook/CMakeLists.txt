# Draconic.Tools.Cook - the command-line cooker (headless: no shell/GPU).
add_executable(Draconic.Tools.Cook Main.cpp)
target_link_libraries(Draconic.Tools.Cook PRIVATE
    Draconic::Core Draconic::Content Draconic::VFS
    Draconic::Pipeline.Core Draconic::Editor.Core Draconic::Pipeline.Cook
    Draconic::Texture.Pipeline Draconic::Fonts.Pipeline Draconic::Image.Pipeline Draconic::Geometry.Pipeline
    Draconic::Animation.Pipeline Draconic::Materials.Pipeline Draconic::Shaders.Pipeline
    Draconic::Particles.Pipeline Draconic::Input Draconic::Input.Resource Draconic::Input.Pipeline Draconic::ModelImporter Draconic::Image.Resource
    Draconic::Physics Draconic::Physics.Resource Draconic::Physics.Pipeline
    Draconic::Audio Draconic::Audio.Resource Draconic::Audio.Pipeline
    Draconic::Script Draconic::Script.Wren Draconic::Script.AngelScript Draconic::Script.Resource Draconic::Script.Facades
    Draconic::Script.Pipeline Draconic::Script.Wren.Pipeline Draconic::Script.AngelScript.Pipeline
    Draconic::UI.Resource Draconic::UI.Pipeline)

