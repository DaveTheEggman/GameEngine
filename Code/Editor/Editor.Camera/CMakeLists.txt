# Editor::Camera - `editor.camera`: the shared free-fly viewport camera (EditorCamera),
# graduated out of Editor::Scene into its own LEAN, foundation-only module so every editor page
# lib can import it without pulling the heavy render/scene/viewport graph. Deliberately minimal:
# keeping it apart from editor.preview is what lets ScenePage (and the other pages) share the
# camera without the GCC module-merge ICE the combined interface triggered. Tooling only.

util_add_engine_library(Editor.Camera ALIAS Editor::Camera)
target_link_libraries(Editor.Camera
    PUBLIC Foundation::Core Foundation::Shell Foundation::Policy)

target_sources(Editor.Camera
    PUBLIC
        FILE_SET editor_camera_modules TYPE CXX_MODULES FILES
            EditorCamera.cppm)
