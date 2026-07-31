# Draconic::EditorAudio - `draconic.editor.audio`: audio clip audition page (waveform + play).
add_library(Draconic.Editor.Audio STATIC)
add_library(Draconic::EditorAudio ALIAS Draconic.Editor.Audio)
target_link_libraries(Draconic.Editor.Audio PUBLIC
    Draconic::Core Draconic::Content Draconic::RuntimeClient Draconic::UI Draconic::UIToolkit
    Draconic::Audio Draconic::AudioEditor Draconic::AudioSubsystem
    Draconic::EditorCore Draconic::EditorApp Draconic::Policy)
target_sources(Draconic.Editor.Audio PRIVATE SoundCuePageImpl.cpp AudioClipPageImpl.cpp BusLayoutPageImpl.cpp PUBLIC FILE_SET editor_audio_modules TYPE CXX_MODULES FILES AudioClipPage.cppm SoundCuePage.cppm BusLayoutPage.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Editor.Audio.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
