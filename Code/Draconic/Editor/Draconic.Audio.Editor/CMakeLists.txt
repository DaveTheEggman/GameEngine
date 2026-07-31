# Draconic::Audio.Editor - `draconic.audio.editor` (tooling: audio clip asset/builder/importer).
add_library(Draconic.Audio.Editor STATIC)
add_library(Draconic::Audio.Editor ALIAS Draconic.Audio.Editor)
target_link_libraries(Draconic.Audio.Editor PUBLIC
    Draconic::Core Draconic::Editor Draconic::Editor.Core Draconic::Content
    Draconic::Audio Draconic::Audio.Resource Draconic::Policy)
target_sources(Draconic.Audio.Editor PUBLIC FILE_SET audio_editor_modules TYPE CXX_MODULES FILES AudioAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Audio.Editor.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
