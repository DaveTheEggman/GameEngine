# Pipeline::Input - `draconic.input.editor`: authored input-map asset + builder (tooling).
add_library(Input.Pipeline STATIC)
add_library(Pipeline::Input ALIAS Input.Pipeline)

target_link_libraries(Input.Pipeline
    PUBLIC Foundation::Core Foundation::Content Pipeline::Core Foundation::Input
           Foundation::Input.Resource Foundation::Policy)

target_sources(Input.Pipeline
    PRIVATE
        InputMapAssetImpl.cpp   # InputMapAsset reflection body (Nested map; GCC hygiene)
    PUBLIC
        FILE_SET input_editor_modules TYPE CXX_MODULES FILES
            InputMapAsset.cppm)
