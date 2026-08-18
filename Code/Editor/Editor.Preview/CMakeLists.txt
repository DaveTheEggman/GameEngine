# Editor::Preview - `editor.preview`: the shared 3D-preview substrate (EditorCamera +
# PreviewViewport) that the bespoke asset editor pages CONTAIN instead of each hand-rolling
# a viewport + preview scene + camera + render loop. Linked by editor page libs (Editor.Scene,
# Editor.Physics, ...); never by the runtime. See Documentation/Specs/editor-preview-viewport.md.

add_library(Editor.Preview STATIC)
add_library(Editor::Preview ALIAS Editor.Preview)

target_link_libraries(Editor.Preview
    PUBLIC
        Foundation::Core Foundation::Shell Foundation::Runtime Foundation::Runtime.Client
        Foundation::Graphics Foundation::RHI Foundation::Scene Engine::Scene
        Foundation::Render Engine::Render Editor::Camera
        Foundation::UI Foundation::UI.Runtime Foundation::UI.Viewport Foundation::VG.Renderer
        Foundation::Policy)

target_sources(Editor.Preview
    PRIVATE
        PreviewViewportImpl.cpp
    PUBLIC
        FILE_SET editor_preview_modules TYPE CXX_MODULES FILES
            PreviewViewport.cppm)
