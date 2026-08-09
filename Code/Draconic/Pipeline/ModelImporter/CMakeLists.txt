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
        Draconic::Core Draconic::Editor.Core Draconic::Policy
        Draconic::Model     Draconic::Model.IO  Draconic::Model.GLTF  Draconic::Model.FBX
        Draconic::Geometry  Draconic::Geometry.Resource     Draconic::Geometry.Pipeline
        Draconic::Materials Draconic::Materials.Resource Draconic::Materials.Pipeline
        Draconic::Texture.Resource  Draconic::Texture.Pipeline  Draconic::Image
        Draconic::Animation Draconic::Animation.Resource Draconic::Animation.Pipeline
        Draconic::Content   Draconic::Resource   Draconic::Pipeline.Core
        Draconic::Model.Resource
        Draconic::Physics   Draconic::Physics.Resource  Draconic::Physics.Pipeline)

target_sources(Draconic.ModelImporter
    PUBLIC
        FILE_SET modelimporter_modules TYPE CXX_MODULES FILES
            FileImport.cppm
            MeshConvert.cppm
            AnimConvert.cppm
            Cook.cppm
            Load.cppm
            ModelImporterModule.cppm)

