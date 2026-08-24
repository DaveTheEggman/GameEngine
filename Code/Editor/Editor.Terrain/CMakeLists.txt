# Editor::Terrain - `editor.terrain`. The TerrainAsset composition + 3D-preview page (references +
# layers + castShadows on the right, a cooked-terrain orbit preview on the left). All UI lives here;
# the Terrain.Pipeline stays UI-free. Brushes are NOT here (scene-viewport IViewportTools, later).
add_library(Editor.Terrain STATIC)
add_library(Editor::Terrain ALIAS Editor.Terrain)

target_link_libraries(Editor.Terrain PUBLIC
    Foundation::Core Foundation::Content
    Foundation::Runtime Foundation::Runtime.Client Foundation::Graphics
    Foundation::Scene Engine::Scene Foundation::Render Engine::Render
    Foundation::UI Foundation::UI.Toolkit Foundation::UI.Runtime
    Editor::Core Editor::App Editor::Camera Editor::Preview Editor::ViewportTools
    Pipeline::Terrain Pipeline::Heightfield Foundation::Terrain Foundation::Terrain.Resource Engine::Terrain
    Foundation::Heightfield Foundation::Heightfield.Resource Foundation::Resource Foundation::Policy)

target_sources(Editor.Terrain
    PRIVATE TerrainEditorPageImpl.cpp SculptImpl.cpp SplatToolImpl.cpp ToolPanelsImpl.cpp
    PUBLIC FILE_SET editorterrain_modules TYPE CXX_MODULES FILES
        TerrainEditorPage.cppm Sculpt.cppm SplatTool.cppm)
