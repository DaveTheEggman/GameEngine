# Foundation::Geometry.Resource - `foundation.geometry.resource` (meshes as resources:
# Static/SkinnedMeshSource -> Static/SkinnedMesh via factories).
util_add_engine_library(Geometry.Resource ALIAS Foundation::Geometry.Resource)
target_link_libraries(Geometry.Resource
    PUBLIC Foundation::Core Foundation::Resource Foundation::Content Foundation::Geometry Foundation::Policy)

target_sources(Geometry.Resource
    PUBLIC
        FILE_SET mesh_resource_modules TYPE CXX_MODULES FILES
            MeshResource.cppm)

