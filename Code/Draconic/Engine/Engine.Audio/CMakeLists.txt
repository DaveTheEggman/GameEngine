# Engine::Audio - `draconic.engine.audio`: scene integration
# (components + per-scene voice group + transform/listener sync + one-shot API).
add_library(Engine.Audio STATIC)
add_library(Engine::Audio ALIAS Engine.Audio)

target_link_libraries(Engine.Audio
    PUBLIC Foundation::Core Foundation::Runtime Foundation::Scene Engine::Scene
           Foundation::Audio Foundation::Resource Foundation::Content Foundation::Script
           Foundation::Script.Facades Foundation::Settings Engine::Render Foundation::Policy)

target_sources(Engine.Audio
    PRIVATE
        AudioSubsystemImpl.cpp   # implementation unit: listener push + reflection (render dep stays out of the interface)
    PUBLIC
        FILE_SET audio_subsystem_modules TYPE CXX_MODULES FILES
            AudioSubsystem.cppm
            AudioComponents.cppm)

