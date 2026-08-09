# Draconic::Input.Pipeline - `draconic.input.editor`: authored input-map asset + builder (tooling).
add_library(Draconic.Input.Pipeline STATIC)
add_library(Draconic::Input.Pipeline ALIAS Draconic.Input.Pipeline)

target_link_libraries(Draconic.Input.Pipeline
    PUBLIC Draconic::Core Draconic::Content Draconic::Pipeline.Core Draconic::Input
           Draconic::Input.Resource Draconic::Policy)

target_sources(Draconic.Input.Pipeline
    PRIVATE
        InputMapAssetImpl.cpp   # InputMapAsset reflection body (Nested map; GCC hygiene)
    PUBLIC
        FILE_SET input_editor_modules TYPE CXX_MODULES FILES
            InputMapAsset.cppm)
