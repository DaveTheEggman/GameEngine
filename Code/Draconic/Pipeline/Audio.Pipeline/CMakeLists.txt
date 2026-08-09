# Pipeline::Audio - `draconic.audio.editor` (tooling: audio clip asset/builder/importer).
add_library(Audio.Pipeline STATIC)
add_library(Pipeline::Audio ALIAS Audio.Pipeline)
target_link_libraries(Audio.Pipeline PUBLIC
    Foundation::Core Pipeline::Core Editor::Core Foundation::Content
    Foundation::Audio Foundation::Audio.Resource Foundation::Policy)
target_sources(Audio.Pipeline
    PRIVATE
        AudioAssetImpl.cpp
    PUBLIC FILE_SET audio_editor_modules TYPE CXX_MODULES FILES AudioAsset.cppm)

