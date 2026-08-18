# Editor::Navigation - `editor.navigation`: the navmesh authoring tools (tooling).
# The "Bake Navigation" action: collect the static-mesh geometry inside a zone's AABB (in
# ZONE-LOCAL space), bake it via Recast, and write the result into the zone's NavigationZoneAsset
# sidecar. Editor-only; never linked by the runtime.
add_library(Editor.Navigation STATIC)
add_library(Editor::Navigation ALIAS Editor.Navigation)
target_link_libraries(Editor.Navigation PUBLIC
    Foundation::Core Foundation::Scene Foundation::Content
    Foundation::Geometry Engine::Render
    Foundation::Navigation Foundation::Navigation.Resource
    Engine::Navigation Pipeline::Navigation Foundation::Policy)
target_sources(Editor.Navigation
    PRIVATE
        NavigationBakeImpl.cpp   # bake logic + heavy imports (kept out of the interface unit)
    PUBLIC
        FILE_SET editor_navigation_modules TYPE CXX_MODULES FILES NavigationBake.cppm)
