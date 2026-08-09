# Tools.Editor - the Draconic editor executable (assembly of draconic.editor.app + the
# per-subsystem editor plugin modules; see docs/design/editor.md §3.1).
# The EMBEDDED fallback font: Roboto baked into the exe as a byte array, so a relocated
# editor always has a face even when every disk path fails (settings path, dev-tree path).
set(_embedded_font_src "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedEditorFont.cpp")
add_custom_command(
    OUTPUT "${_embedded_font_src}"
    COMMAND ${CMAKE_COMMAND}
        -DIN=${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf
        -DOUT=${_embedded_font_src}
        -DSYMBOL=g_draconicEmbeddedEditorFont
        -P ${CMAKE_SOURCE_DIR}/cmake/EmbedBinary.cmake
    DEPENDS "${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf"
            "${CMAKE_SOURCE_DIR}/cmake/EmbedBinary.cmake"
    COMMENT "Embedding the editor fallback font")

add_executable(Tools.Editor Main.cpp "${_embedded_font_src}")
target_link_libraries(Tools.Editor PRIVATE
    Foundation::Core
    Foundation::Runtime Foundation::Runtime.Client Foundation::Runtime.Desktop Foundation::Shell.Desktop
    Foundation::Shell Foundation::Graphics Foundation::Graphics.Gpu
    Engine::Scene Engine::Render Engine::Animation Engine::Particles
    Editor::Core Editor::App Editor::Scene
    Pipeline::Core Pipeline::Cook
    Foundation::Resource Foundation::Geometry.Resource Foundation::Materials.Resource Foundation::Texture.Resource
    Pipeline::Texture Pipeline::Fonts Pipeline::Image Pipeline::Geometry
    Pipeline::Animation Pipeline::Materials Pipeline::Shaders
    Pipeline::Particles Foundation::Input Foundation::Input.Resource Pipeline::Input Editor::Input Engine::Input Foundation::Physics Foundation::Physics.Resource Pipeline::Physics Engine::Physics Foundation::Audio Foundation::Audio.Resource Pipeline::Audio Engine::Audio
    Foundation::Script Foundation::Script.Wren Foundation::Script.AngelScript Foundation::Script.Resource Foundation::Script.Facades Pipeline::Script Pipeline::Script.Wren Editor::Script.Wren Pipeline::Script.AngelScript Editor::Script.AngelScript Engine::Script Editor::Script Foundation::UI.Resource Pipeline::UI Editor::GameUI Editor::Audio Editor::Texture Editor::Image Editor::Fonts Editor::Physics Editor::Generic Pipeline::ModelImporter Foundation::Image.Resource
    Foundation::Animation.Resource)
target_compile_definitions(Tools.Editor PRIVATE
    DRACONIC_EDITOR_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf"
    DRACONIC_EDITOR_BASELINE_ASSETS="${CMAKE_SOURCE_DIR}/Data/Assets"
    DRACONIC_EDITOR_MONO_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/dejavu/DejaVuSansMono.ttf")
# Copy SDL3.dll next to the executable on Windows (same as the samples' helper, which is
# defined in Code/Samples/ and not visible here).
draconic_copy_runtime_deps(Tools.Editor EXTRA ${DRACONIC_SDL3_DLL})

