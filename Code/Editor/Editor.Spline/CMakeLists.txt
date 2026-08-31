# Editor::Spline - `editor.spline`: the interactive spline viewport tool (drag / insert /
# delete control points over SplineComponent, one undo command per gesture). Editor-only.

add_library(Editor.Spline STATIC)
add_library(Editor::Spline ALIAS Editor.Spline)

target_link_libraries(Editor.Spline PUBLIC
    Foundation::Core Foundation::Scene Foundation::Render Foundation::Shell
    Foundation::Spline Engine::Spline
    Editor::Core Editor::ViewportTools Foundation::Policy)

target_sources(Editor.Spline
    PRIVATE
        SplineToolImpl.cpp
    PUBLIC
        FILE_SET editor_spline_modules TYPE CXX_MODULES FILES
            SplineTool.cppm)
