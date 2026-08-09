# Foundation::Geometry.Resource - `draconic.geometry.resource` (meshes as resources:
# Static/SkinnedMeshSource -> Static/SkinnedMesh via factories).
add_library(Geometry.Resource STATIC)
add_library(Foundation::Geometry.Resource ALIAS Geometry.Resource)

target_link_libraries(Geometry.Resource
    PUBLIC Foundation::Core Foundation::Resource Foundation::Content Foundation::Geometry Foundation::Policy)

target_sources(Geometry.Resource
    PUBLIC
        FILE_SET mesh_resource_modules TYPE CXX_MODULES FILES
            MeshResource.cppm)

