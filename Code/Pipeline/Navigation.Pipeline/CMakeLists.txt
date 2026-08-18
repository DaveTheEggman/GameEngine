# Pipeline::Navigation - `navigation.pipeline` (tooling: navmesh-zone asset + builder).
# The zone is authored in-scene and baked by the editor; this asset carries the baked navmesh
# (as a sidecar stream - bulk-data-sidecar rule) and cooks it through to NavigationZoneSource.
# No OS-file importer. Never linked by the runtime.
add_library(Navigation.Pipeline STATIC)
add_library(Pipeline::Navigation ALIAS Navigation.Pipeline)
target_link_libraries(Navigation.Pipeline PUBLIC
    Foundation::Core Pipeline::Core Foundation::Content
    Foundation::Navigation Foundation::Navigation.Resource Foundation::Policy)
target_sources(Navigation.Pipeline PUBLIC
    FILE_SET navigation_pipeline_modules TYPE CXX_MODULES FILES NavigationAsset.cppm)
