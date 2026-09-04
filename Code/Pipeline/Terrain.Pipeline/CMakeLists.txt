# Pipeline::Terrain - `terrain.pipeline` (tooling: TerrainAsset + builder; a reference pass-through).
util_add_engine_library(Terrain.Pipeline ALIAS Pipeline::Terrain)
target_link_libraries(Terrain.Pipeline PUBLIC
    Foundation::Core Foundation::VFS Pipeline::Core Pipeline::Importer
    Foundation::Image Foundation::Image.IO Pipeline::Texture
    Foundation::Terrain.Resource Foundation::Content Foundation::Policy)
target_sources(Terrain.Pipeline
    PRIVATE
        TerrainAssetImpl.cpp
    PUBLIC FILE_SET terrain_pipeline_modules TYPE CXX_MODULES FILES TerrainAsset.cppm)
