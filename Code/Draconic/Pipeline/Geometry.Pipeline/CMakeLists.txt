# Draconic::Geometry.Pipeline - `draconic.geometry.editor` (tooling: Static/SkinnedMeshAsset ->
# Static/SkinnedMeshSource cook). Never linked by the runtime.
add_library(Draconic.Geometry.Pipeline STATIC)
add_library(Draconic::Geometry.Pipeline ALIAS Draconic.Geometry.Pipeline)

target_link_libraries(Draconic.Geometry.Pipeline
    PUBLIC Draconic::Core Draconic::Pipeline.Core Draconic::Content
           Draconic::Geometry Draconic::Geometry.Resource Draconic::Policy)

target_sources(Draconic.Geometry.Pipeline
    PUBLIC
        FILE_SET mesh_editor_modules TYPE CXX_MODULES FILES
            MeshAsset.cppm)

