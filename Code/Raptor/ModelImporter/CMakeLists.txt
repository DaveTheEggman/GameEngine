# Raptor::ModelImporter — `raptor.modelimporter`, the top-level model import pipeline.
# Bridges the model loaders (raptor.model IR) to the engine's cooked resources: it
# converts a loaded Model into the geometry/material/texture/animation *Source types
# and cooks them through the editor stack into a content database. This is the only
# library that depends on BOTH raptor.model (the loader IR) and the engine resource
# stack, keeping that dependency out of the lower libraries.
add_library(RaptorModelImporter STATIC)
add_library(Raptor::ModelImporter ALIAS RaptorModelImporter)

target_link_libraries(RaptorModelImporter
    PUBLIC
        Raptor::Core Raptor::Policy
        Raptor::Model     Raptor::ModelIO  Raptor::ModelGltf  Raptor::ModelFbx
        Raptor::Geometry  Raptor::MeshResource  Raptor::MeshEditor
        Raptor::Content   Raptor::Resource   Raptor::Editor)

target_sources(RaptorModelImporter
    PUBLIC
        FILE_SET modelimporter_modules TYPE CXX_MODULES FILES
            MeshConvert.cppm
            ModelResource.cppm
            Cook.cppm
            Load.cppm
            ModelImporterModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
