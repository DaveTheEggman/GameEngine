# Raptor::MeshResource — `raptor.geometry.resource` (meshes as resources:
# Static/SkinnedMeshSource -> Static/SkinnedMesh via factories).
add_library(RaptorMeshResource STATIC)
add_library(Raptor::MeshResource ALIAS RaptorMeshResource)

target_link_libraries(RaptorMeshResource
    PUBLIC Raptor::Core Raptor::Resource Raptor::Content Raptor::Geometry Raptor::Policy)

target_sources(RaptorMeshResource
    PUBLIC
        FILE_SET mesh_resource_modules TYPE CXX_MODULES FILES
            MeshResource.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
