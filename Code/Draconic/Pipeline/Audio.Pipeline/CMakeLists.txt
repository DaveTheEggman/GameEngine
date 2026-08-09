# Draconic::Audio.Pipeline - `draconic.audio.editor` (tooling: audio clip asset/builder/importer).
add_library(Draconic.Audio.Pipeline STATIC)
add_library(Draconic::Audio.Pipeline ALIAS Draconic.Audio.Pipeline)
target_link_libraries(Draconic.Audio.Pipeline PUBLIC
    Draconic::Core Draconic::Pipeline.Core Draconic::Editor.Core Draconic::Content
    Draconic::Audio Draconic::Audio.Resource Draconic::Policy)
target_sources(Draconic.Audio.Pipeline
    PRIVATE
        AudioAssetImpl.cpp
    PUBLIC FILE_SET audio_editor_modules TYPE CXX_MODULES FILES AudioAsset.cppm)

