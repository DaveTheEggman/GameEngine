# Pipeline::Geometry - `geometry.pipeline` (tooling: Static/SkinnedMeshAsset ->
# Static/SkinnedMeshSource cook). Never linked by the runtime.
add_library(Geometry.Pipeline STATIC)
add_library(Pipeline::Geometry ALIAS Geometry.Pipeline)

target_link_libraries(Geometry.Pipeline
    PUBLIC Foundation::Core Pipeline::Core Foundation::Content
           Foundation::Geometry Foundation::Geometry.Resource Foundation::Policy
    # Cook-time only (mesh-lod.md P0); PRIVATE + impl-unit include keeps the vendored
    # header out of the module interface and out of every consumer's link line.
    PRIVATE ThirdParty::MeshOptimizer)

target_sources(Geometry.Pipeline
    PRIVATE
        MeshOptimizeImpl.cpp   # P0 optimization pass (meshoptimizer lives here only)
    PUBLIC
        FILE_SET mesh_editor_modules TYPE CXX_MODULES FILES
            MeshAsset.cppm)

