# Editor::ViewportTools - the viewport tool-mode framework (editor.viewporttools).
#
# The THIN layer between Editor.Core (UI-free - headless hosts link it, so tool contracts that
# speak pick rays and overlay drawing cannot live there) and the pages: IViewportTool + manager +
# provider registry. Domain editor libs (Editor.Terrain, ...) implement tools against this
# interface without linking any page; the scene page hosts the manager. No ui.toolkit dependency:
# input is a plain struct the host fills (the GizmoFrameInput precedent).

util_add_engine_library(Editor.ViewportTools ALIAS Editor::ViewportTools)
target_link_libraries(Editor.ViewportTools
    PUBLIC
        Foundation::Core Foundation::Scene Foundation::Render Foundation::Shell
        Editor::Core Foundation::Policy)

target_sources(Editor.ViewportTools
    PRIVATE
        ViewportToolsImpl.cpp
    PUBLIC
        FILE_SET editor_viewporttools_modules TYPE CXX_MODULES FILES
            ViewportTools.cppm)
