# Draconic::ModelImporter - `draconic.modelimporter`, the top-level model import pipeline.
# Bridges the model loaders (draconic.model IR) to the engine's cooked resources: it
# converts a loaded Model into the geometry/material/texture/animation *Source types
# and cooks them through the editor stack into a content database. This is the only
# library that depends on BOTH draconic.model (the loader IR) and the engine resource
# stack, keeping that dependency out of the lower libraries.
add_library(Draconic.ModelImporter STATIC)
add_library(Draconic::ModelImporter ALIAS Draconic.ModelImporter)

target_link_libraries(Draconic.ModelImporter
    PUBLIC
        Draconic::Core Draconic::EditorCore Draconic::Policy
        Draconic::Model     Draconic::ModelIO  Draconic::ModelGltf  Draconic::ModelFbx
        Draconic::Geometry  Draconic::MeshResource     Draconic::MeshEditor
        Draconic::Materials Draconic::MaterialResource Draconic::MaterialEditor
        Draconic::TextureResource  Draconic::TextureEditor  Draconic::Image
        Draconic::Animation Draconic::AnimationResource Draconic::AnimationEditor
        Draconic::Content   Draconic::Resource   Draconic::Editor
        Draconic::ModelResource
        Draconic::Physics   Draconic::PhysicsResource  Draconic::PhysicsEditor)

target_sources(Draconic.ModelImporter
    PUBLIC
        FILE_SET modelimporter_modules TYPE CXX_MODULES FILES
            FileImport.cppm
            MeshConvert.cppm
            AnimConvert.cppm
            Cook.cppm
            Load.cppm
            ModelImporterModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.ModelImporter.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
