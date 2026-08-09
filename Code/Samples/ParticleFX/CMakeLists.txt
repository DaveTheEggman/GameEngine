# ParticleFX - the particle-system showcase (CPU-sim fountain drawn by the dedicated ParticleRenderer).
# Same host set as the other samples + the particle core and its scene/render integration.

add_executable(ParticleFX main.cpp)

target_link_libraries(ParticleFX PRIVATE
    Foundation::Core
    Foundation::Runtime
    Foundation::Runtime.Client
    Foundation::Shell.Desktop Foundation::Runtime.Desktop
    Foundation::Graphics
    Foundation::Graphics.Gpu
    Engine::DefaultApp
    Foundation::Scene
    Engine::Scene
    Engine::Render
    Extensions::Imgui
    Foundation::Geometry
    Foundation::Materials
    Foundation::Particles
    Engine::Particles
    Foundation::Particles.Resource       # cooked ParticleEffectResource + factory
    Pipeline::Particles         # ParticleEffectAsset + bake (sample references editor for the demo)
    Pipeline::Core Foundation::Content Foundation::Resource Foundation::VFS
    Foundation::Policy)

# Per-sample cooked-output DB (isolated), matching the other samples' pattern.
target_compile_definitions(ParticleFX PRIVATE
    DRACONIC_PARTICLEFX_OUTPUT_DIR="${CMAKE_SOURCE_DIR}/Data/Output/ParticleFX")

draconic_copy_sdl3_dll(ParticleFX)
