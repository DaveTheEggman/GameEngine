# Draconic::Geometry - `draconic.geometry`, the engine's runtime mesh format
# (StaticMesh/SkinnedMesh + index buffer + procedural primitives). Pure data on
# top of Core; the renderer uploads the raw vertex/index streams.
add_library(Draconic.Geometry STATIC)
add_library(Draconic::Geometry ALIAS Draconic.Geometry)

target_link_libraries(Draconic.Geometry
    PUBLIC Draconic::Core Draconic::Policy)

target_sources(Draconic.Geometry
    PUBLIC
        FILE_SET geometry_modules TYPE CXX_MODULES FILES
            Types.cppm
            IndexBuffer.cppm
            Mesh.cppm
            Primitives.cppm
            GeometryModule.cppm)

# Meshes as resources (Static/SkinnedMeshSource -> Static/SkinnedMesh).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Geometry.Resource ${CMAKE_CURRENT_BINARY_DIR}/Resource)

# Mesh authoring + cook (tooling: Static/SkinnedMeshAsset -> source).
if(DRACONIC_BUILD_TOOLS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../Editor/Draconic.Geometry.Editor ${CMAKE_CURRENT_BINARY_DIR}/Editor)
endif()
