# Draconic.Tools.Editor - the Draconic editor executable (assembly of draconic.editor.app + the
# per-subsystem editor plugin modules; see docs/design/editor.md §3.1).
add_executable(Draconic.Tools.Editor Main.cpp)
target_link_libraries(Draconic.Tools.Editor PRIVATE
    Draconic::Core
    Draconic::Runtime Draconic::Runtime.Client Draconic::Runtime.Desktop Draconic::Shell.Desktop
    Draconic::Shell Draconic::Graphics Draconic::Graphics.Gpu
    Draconic::Engine.Scene Draconic::Engine.Render Draconic::Engine.Animation Draconic::Engine.Particles
    Draconic::Editor.Core Draconic::Editor.App Draconic::Editor.Scene
    Draconic::Editor Draconic::Editor.Cook
    Draconic::Resource Draconic::Geometry.Resource Draconic::Materials.Resource Draconic::Texture.Resource
    Draconic::Texture.Editor Draconic::Image.Editor Draconic::Geometry.Editor
    Draconic::Animation.Editor Draconic::Materials.Editor Draconic::Shaders.Editor
    Draconic::Particles.Editor Draconic::Input Draconic::Input.Resource Draconic::Input.Editor Draconic::Editor.Input Draconic::Engine.Input Draconic::Physics Draconic::Physics.Resource Draconic::Physics.Editor Draconic::Engine.Physics Draconic::Audio Draconic::Audio.Resource Draconic::Audio.Editor Draconic::Engine.Audio
    Draconic::Script Draconic::Script.Wren Draconic::Script.AngelScript Draconic::Script.Resource Draconic::Script.Facades Draconic::Script.Editor Draconic::Script.Wren.Editor Draconic::Script.Wren.Editor.UI Draconic::Script.AngelScript.Editor Draconic::Script.AngelScript.Editor.UI Draconic::Engine.Script Draconic::Editor.Script Draconic::UI.Resource Draconic::UI.Editor Draconic::Editor.GameUI Draconic::Editor.Audio Draconic::Editor.Texture Draconic::Editor.Image Draconic::Editor.Generic Draconic::ModelImporter Draconic::Image.Resource
    Draconic::Animation.Resource)
target_compile_definitions(Draconic.Tools.Editor PRIVATE
    DRACONIC_EDITOR_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf"
    DRACONIC_EDITOR_MONO_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/dejavu/DejaVuSansMono.ttf")
# Copy SDL3.dll next to the executable on Windows (same as the samples' helper, which is
# defined in Code/Samples/ and not visible here).
draconic_copy_runtime_deps(Draconic.Tools.Editor EXTRA ${DRACONIC_SDL3_DLL})

set_target_properties(Draconic.Tools.Editor PROPERTIES OUTPUT_NAME RaptorEditor)
