# Draconic::AudioEditor - `draconic.audio.editor` (tooling: audio clip asset/builder/importer).
add_library(DraconicAudioEditor STATIC)
add_library(Draconic::AudioEditor ALIAS DraconicAudioEditor)
target_link_libraries(DraconicAudioEditor PUBLIC
    Draconic::Core Draconic::Editor Draconic::EditorCore Draconic::Content
    Draconic::Audio Draconic::AudioResource Draconic::Policy)
target_sources(DraconicAudioEditor PUBLIC FILE_SET audio_editor_modules TYPE CXX_MODULES FILES AudioAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
