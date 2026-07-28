# Draconic::EditorAudio - `draconic.editor.audio`: audio clip audition page (waveform + play).
add_library(DraconicEditorAudio STATIC)
add_library(Draconic::EditorAudio ALIAS DraconicEditorAudio)
target_link_libraries(DraconicEditorAudio PUBLIC
    Draconic::Core Draconic::Content Draconic::RuntimeClient Draconic::UI Draconic::UIToolkit
    Draconic::Audio Draconic::AudioEditor Draconic::AudioSubsystem
    Draconic::EditorCore Draconic::EditorApp Draconic::Policy)
target_sources(DraconicEditorAudio PRIVATE SoundCuePageImpl.cpp AudioClipPageImpl.cpp BusLayoutPageImpl.cpp PUBLIC FILE_SET editor_audio_modules TYPE CXX_MODULES FILES AudioClipPage.cppm SoundCuePage.cppm BusLayoutPage.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
