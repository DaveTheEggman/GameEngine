# Draconic::MeshEditor — `draconic.geometry.editor` (tooling: Static/SkinnedMeshAsset ->
# Static/SkinnedMeshSource cook). Never linked by the runtime.
add_library(DraconicMeshEditor STATIC)
add_library(Draconic::MeshEditor ALIAS DraconicMeshEditor)

target_link_libraries(DraconicMeshEditor
    PUBLIC Draconic::Core Draconic::Editor Draconic::Content
           Draconic::Geometry Draconic::MeshResource Draconic::Policy)

target_sources(DraconicMeshEditor
    PUBLIC
        FILE_SET mesh_editor_modules TYPE CXX_MODULES FILES
            MeshAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
