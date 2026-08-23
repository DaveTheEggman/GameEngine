# Foundation::Heightfield.Resource - `foundation.heightfield.resource`: the heightfield as a cooked,
# referenceable resource (HeightfieldSource -> Heightfield via HeightfieldFactory). What Ref<Heightfield>
# resolves to for terrain, the physics collider, and the nav bake.
add_library(Heightfield.Resource STATIC)
add_library(Foundation::Heightfield.Resource ALIAS Heightfield.Resource)

target_link_libraries(Heightfield.Resource
    PUBLIC Foundation::Core Foundation::Resource Foundation::Content Foundation::Heightfield Foundation::Policy)

target_sources(Heightfield.Resource
    PUBLIC
        FILE_SET heightfield_resource_modules TYPE CXX_MODULES FILES
            HeightfieldResource.cppm)
