# Engine::Terrain - `engine.terrain`. PHASE A: the TerrainComponent (references a cooked Terrain
# resource) + scene integration. The chunked geo-mipmap renderer (RHI) lands in a later phase.
add_library(Engine.Terrain STATIC)
add_library(Engine::Terrain ALIAS Engine.Terrain)

target_link_libraries(Engine.Terrain
    PUBLIC
        Foundation::Core Foundation::Scene Foundation::Resource
        Foundation::Terrain Foundation::Terrain.Resource Foundation::Heightfield
        Foundation::RHI Foundation::Policy)

target_sources(Engine.Terrain
    PRIVATE
        TerrainComponentsImpl.cpp
    PUBLIC
        FILE_SET engineterrain_modules TYPE CXX_MODULES FILES
            HeightTexture.cppm
            TerrainComponents.cppm)
