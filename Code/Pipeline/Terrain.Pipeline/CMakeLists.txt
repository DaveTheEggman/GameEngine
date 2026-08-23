# Pipeline::Terrain - `terrain.pipeline` (tooling: TerrainAsset + builder; a reference pass-through).
add_library(Terrain.Pipeline STATIC)
add_library(Pipeline::Terrain ALIAS Terrain.Pipeline)
target_link_libraries(Terrain.Pipeline PUBLIC
    Foundation::Core Foundation::VFS Pipeline::Core
    Foundation::Terrain.Resource Foundation::Content Foundation::Policy)
target_sources(Terrain.Pipeline
    PRIVATE
        TerrainAssetImpl.cpp
    PUBLIC FILE_SET terrain_pipeline_modules TYPE CXX_MODULES FILES TerrainAsset.cppm)
