# Raptor::MeshEditor — `raptor.geometry.editor` (tooling: Static/SkinnedMeshAsset ->
# Static/SkinnedMeshSource cook). Never linked by the runtime.
add_library(RaptorMeshEditor STATIC)
add_library(Raptor::MeshEditor ALIAS RaptorMeshEditor)

target_link_libraries(RaptorMeshEditor
    PUBLIC Raptor::Core Raptor::Editor Raptor::Content
           Raptor::Geometry Raptor::MeshResource Raptor::Policy)

target_sources(RaptorMeshEditor
    PUBLIC
        FILE_SET mesh_editor_modules TYPE CXX_MODULES FILES
            MeshAsset.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
