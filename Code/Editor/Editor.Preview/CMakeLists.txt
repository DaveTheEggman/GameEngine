# Editor::Preview - `editor.preview`: the shared 3D-preview substrate (EditorCamera +
# PreviewViewport) that the bespoke asset editor pages CONTAIN instead of each hand-rolling
# a viewport + preview scene + camera + render loop. Linked by editor page libs (Editor.Scene,
# Editor.Physics, ...); never by the runtime.

util_add_engine_library(Editor.Preview ALIAS Editor::Preview)
target_link_libraries(Editor.Preview
    PUBLIC
        Foundation::Core Foundation::Shell Foundation::Runtime Foundation::Runtime.Client
        Foundation::Graphics Foundation::RHI Foundation::Scene Engine::Scene
        Foundation::Render Engine::Render Editor::Camera
        Foundation::UI Foundation::UI.Runtime Foundation::UI.Viewport Foundation::VG.Renderer
        Foundation::Image Foundation::Resource Editor::Core
        Foundation::Policy)

target_sources(Editor.Preview
    PRIVATE
        PreviewViewportImpl.cpp
        ThumbnailStageImpl.cpp
    PUBLIC
        FILE_SET editor_preview_modules TYPE CXX_MODULES FILES
            PreviewViewport.cppm
            ThumbnailStage.cppm)
