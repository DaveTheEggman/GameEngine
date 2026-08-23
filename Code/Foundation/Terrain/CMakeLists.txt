# Foundation::Terrain - `foundation.terrain`, the terrain model over a heightfield: the chunk grid +
# quadtree the chunked geo-mipmap renderer draws, per-chunk LOD selection, and splat descriptors.
# Pure data + math on top of Core + Heightfield, NO RHI.
add_library(Terrain STATIC)
add_library(Foundation::Terrain ALIAS Terrain)

target_link_libraries(Terrain
    PUBLIC Foundation::Core Foundation::Heightfield Foundation::Policy)

target_sources(Terrain
    PUBLIC
        FILE_SET terrain_modules TYPE CXX_MODULES FILES
            Terrain.cppm)
