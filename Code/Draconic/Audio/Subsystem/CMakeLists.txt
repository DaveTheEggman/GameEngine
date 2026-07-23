# Draconic::AudioSubsystem - `draconic.audio.subsystem`: scene integration
# (components + per-scene voice group + transform/listener sync + one-shot API).
add_library(DraconicAudioSubsystem STATIC)
add_library(Draconic::AudioSubsystem ALIAS DraconicAudioSubsystem)

target_link_libraries(DraconicAudioSubsystem
    PUBLIC Draconic::Core Draconic::Runtime Draconic::Scene Draconic::SceneSubsystem
           Draconic::Audio Draconic::Resource Draconic::Content Draconic::Script
           Draconic::Settings Draconic::RenderSubsystem Draconic::Policy)

target_sources(DraconicAudioSubsystem
    PRIVATE
        AudioSubsystemImpl.cpp   # implementation unit: listener push + reflection (render dep stays out of the interface)
    PUBLIC
        FILE_SET audio_subsystem_modules TYPE CXX_MODULES FILES
            AudioSubsystem.cppm
            Components.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
