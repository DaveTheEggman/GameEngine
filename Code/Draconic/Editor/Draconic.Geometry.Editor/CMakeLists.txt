# Draconic::Geometry.Editor - `draconic.geometry.editor` (tooling: Static/SkinnedMeshAsset ->
# Static/SkinnedMeshSource cook). Never linked by the runtime.
add_library(Draconic.Geometry.Editor STATIC)
add_library(Draconic::Geometry.Editor ALIAS Draconic.Geometry.Editor)

target_link_libraries(Draconic.Geometry.Editor
    PUBLIC Draconic::Core Draconic::Editor Draconic::Content
           Draconic::Geometry Draconic::Geometry.Resource Draconic::Policy)

target_sources(Draconic.Geometry.Editor
    PUBLIC
        FILE_SET mesh_editor_modules TYPE CXX_MODULES FILES
            MeshAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Geometry.Editor.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
