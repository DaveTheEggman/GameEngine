# Foundation::Terrain.Resource - `foundation.terrain.resource`: the cooked terrain resource (a bundle
# of references - heightfield + splatmap + per-layer albedo - resolved through the manager). What
# Ref<Terrain> resolves to for the TerrainComponent.
add_library(Terrain.Resource STATIC)
add_library(Foundation::Terrain.Resource ALIAS Terrain.Resource)

target_link_libraries(Terrain.Resource
    PUBLIC Foundation::Core Foundation::Resource Foundation::Content
           Foundation::Heightfield Foundation::Texture.Resource Foundation::Policy)

target_sources(Terrain.Resource
    PUBLIC
        FILE_SET terrain_resource_modules TYPE CXX_MODULES FILES
            TerrainResource.cppm)
