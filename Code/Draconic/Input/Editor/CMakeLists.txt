# Draconic::InputEditor - `draconic.input.editor`: authored input-map asset + builder (tooling).
add_library(DraconicInputEditor STATIC)
add_library(Draconic::InputEditor ALIAS DraconicInputEditor)

target_link_libraries(DraconicInputEditor
    PUBLIC Draconic::Core Draconic::Content Draconic::Editor Draconic::Input
           Draconic::InputResource Draconic::Policy)

target_sources(DraconicInputEditor
    PUBLIC
        FILE_SET input_editor_modules TYPE CXX_MODULES FILES
            InputMapAsset.cppm)
