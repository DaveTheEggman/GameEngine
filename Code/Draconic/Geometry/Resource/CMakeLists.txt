# Draconic::MeshResource - `draconic.geometry.resource` (meshes as resources:
# Static/SkinnedMeshSource -> Static/SkinnedMesh via factories).
add_library(DraconicMeshResource STATIC)
add_library(Draconic::MeshResource ALIAS DraconicMeshResource)

target_link_libraries(DraconicMeshResource
    PUBLIC Draconic::Core Draconic::Resource Draconic::Content Draconic::Geometry Draconic::Policy)

target_sources(DraconicMeshResource
    PUBLIC
        FILE_SET mesh_resource_modules TYPE CXX_MODULES FILES
            MeshResource.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
