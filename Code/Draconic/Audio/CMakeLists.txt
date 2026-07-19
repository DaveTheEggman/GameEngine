# Draconic::Audio - `draconic.audio`: the miniaudio-backed audio engine core.
# miniaudio is PRIVATE: ma_* types never cross the module surface.
add_library(DraconicAudio STATIC)
add_library(Draconic::Audio ALIAS DraconicAudio)

target_link_libraries(DraconicAudio
    PUBLIC Draconic::Core Draconic::VFS Draconic::Policy
    PRIVATE Draconic::Miniaudio)

target_sources(DraconicAudio
    PRIVATE
        EngineImpl.cpp      # module IMPLEMENTATION unit: all miniaudio contact (see Engine.cppm)
        MiniaudioImpl.cpp   # plain TU: MINIAUDIO_IMPLEMENTATION + stb_vorbis (exactly once)
    PUBLIC
        FILE_SET audio_modules TYPE CXX_MODULES FILES
            AudioModule.cppm
            Clip.cppm
            Engine.cppm)

# stb_vorbis.c is included from ThirdParty/stb (no INTERFACE target carries that dir for
# sources; add it directly for the implementation TU).
target_include_directories(DraconicAudio PRIVATE ${CMAKE_SOURCE_DIR}/ThirdParty/stb)

# Vendored implementation code builds without our -Werror strictness.
set_source_files_properties(MiniaudioImpl.cpp PROPERTIES COMPILE_OPTIONS "-Wno-error")

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
