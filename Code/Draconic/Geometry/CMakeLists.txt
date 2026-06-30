# Draconic::Geometry — `draconic.geometry`, the engine's runtime mesh format
# (StaticMesh/SkinnedMesh + index buffer + procedural primitives). Pure data on
# top of Core; the renderer uploads the raw vertex/index streams.
add_library(DraconicGeometry STATIC)
add_library(Draconic::Geometry ALIAS DraconicGeometry)

target_link_libraries(DraconicGeometry
    PUBLIC Draconic::Core Draconic::Policy)

target_sources(DraconicGeometry
    PUBLIC
        FILE_SET geometry_modules TYPE CXX_MODULES FILES
            Types.cppm
            IndexBuffer.cppm
            Mesh.cppm
            Primitives.cppm
            GeometryModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Meshes as resources (Static/SkinnedMeshSource -> Static/SkinnedMesh).
add_subdirectory(Resource)

# Mesh authoring + cook (tooling: Static/SkinnedMeshAsset -> source).
add_subdirectory(Editor)
