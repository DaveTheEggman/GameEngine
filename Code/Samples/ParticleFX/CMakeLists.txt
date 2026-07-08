# ParticleFX - the particle-system showcase (CPU-sim fountain drawn by the dedicated ParticleRenderer).
# Same host set as the other samples + the particle core and its scene/render integration.

add_executable(ParticleFX main.cpp)

target_link_libraries(ParticleFX PRIVATE
    Draconic::Core
    Draconic::Runtime
    Draconic::RuntimeClient
    Draconic::ShellDesktop Draconic::RuntimeDesktop
    Draconic::Graphics
    Draconic::GraphicsGpu
    Draconic::RuntimeDefaultApp
    Draconic::Scene
    Draconic::SceneSubsystem
    Draconic::RenderSubsystem
    Draconic::Imgui
    Draconic::Geometry
    Draconic::Materials
    Draconic::Particles
    Draconic::ParticlesSubsystem
    Draconic::ParticlesResource       # cooked ParticleEffectResource + factory
    Draconic::ParticlesEditor         # ParticleEffectAsset + bake (sample references editor for the demo)
    Draconic::Editor Draconic::Content Draconic::Resource Draconic::VFS
    Draconic::Policy)

# Per-sample cooked-output DB (isolated), matching the other samples' pattern.
target_compile_definitions(ParticleFX PRIVATE
    DRACONIC_PARTICLEFX_OUTPUT_DIR="${CMAKE_SOURCE_DIR}/Data/Output/ParticleFX")

draconic_copy_sdl3_dll(ParticleFX)
