# Draconic::Materials.Pipeline - `draconic.materials.editor` (tooling: MaterialAsset ->
# MaterialSource cook). Never linked by the runtime.
add_library(Draconic.Materials.Pipeline STATIC)
add_library(Draconic::Materials.Pipeline ALIAS Draconic.Materials.Pipeline)

target_link_libraries(Draconic.Materials.Pipeline
    PUBLIC Draconic::Core Draconic::Pipeline.Core Draconic::Content
           Draconic::Materials Draconic::Materials.Resource Draconic::Policy)

target_sources(Draconic.Materials.Pipeline
    PRIVATE
        MaterialAssetImpl.cpp   # MaterialAsset reflection body (Nested source; GCC hygiene)
    PUBLIC
        FILE_SET material_editor_modules TYPE CXX_MODULES FILES
            MaterialAsset.cppm)

