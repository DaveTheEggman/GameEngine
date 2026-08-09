# Pipeline::Geometry - `geometry.pipeline` (tooling: Static/SkinnedMeshAsset ->
# Static/SkinnedMeshSource cook). Never linked by the runtime.
add_library(Geometry.Pipeline STATIC)
add_library(Pipeline::Geometry ALIAS Geometry.Pipeline)

target_link_libraries(Geometry.Pipeline
    PUBLIC Foundation::Core Pipeline::Core Foundation::Content
           Foundation::Geometry Foundation::Geometry.Resource Foundation::Policy)

target_sources(Geometry.Pipeline
    PUBLIC
        FILE_SET mesh_editor_modules TYPE CXX_MODULES FILES
            MeshAsset.cppm)

