# Draconic::AudioResource - `draconic.audio.resource` (cooked audio clips + factory).
add_library(DraconicAudioResource STATIC)
add_library(Draconic::AudioResource ALIAS DraconicAudioResource)
target_link_libraries(DraconicAudioResource PUBLIC
    Draconic::Core Draconic::Resource Draconic::Content Draconic::Audio Draconic::Policy)
target_sources(DraconicAudioResource PUBLIC FILE_SET audio_resource_modules TYPE CXX_MODULES FILES AudioResource.cppm)
