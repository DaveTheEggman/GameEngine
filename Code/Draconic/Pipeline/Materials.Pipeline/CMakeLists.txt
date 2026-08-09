# Pipeline::Materials - `materials.pipeline` (tooling: MaterialAsset ->
# MaterialSource cook). Never linked by the runtime.
add_library(Materials.Pipeline STATIC)
add_library(Pipeline::Materials ALIAS Materials.Pipeline)

target_link_libraries(Materials.Pipeline
    PUBLIC Foundation::Core Pipeline::Core Foundation::Content
           Foundation::Materials Foundation::Materials.Resource Foundation::Policy)

target_sources(Materials.Pipeline
    PRIVATE
        MaterialAssetImpl.cpp   # MaterialAsset reflection body (Nested source; GCC hygiene)
    PUBLIC
        FILE_SET material_editor_modules TYPE CXX_MODULES FILES
            MaterialAsset.cppm)

