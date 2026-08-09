# Foundation::Particles - `foundation.particles`, the CPU particle runtime (a port of
# SedulousEngine's Sedulous.Particles): SoA stream container + initializer/behavior
# modules + effect/system/emitter/instance + CPU simulator. Pure logic over Core; the
# cooked resource (.resource), the GPU-compute sim, the ECS/render integration, and the
# authoring editor all layer on top. See docs/design/particles.md.
add_library(Particles STATIC)
add_library(Foundation::Particles ALIAS Particles)

target_link_libraries(Particles
    PUBLIC Foundation::Core Foundation::Policy)

target_sources(Particles
    PRIVATE
        ParticleModulesImpl.cpp   # module + range-type reflection bodies (GCC hygiene)
        ParticleEffectReflectionImpl.cpp # effect/system/emitter reflection bodies (GCC hygiene)
    PUBLIC
        FILE_SET particles_modules TYPE CXX_MODULES FILES
            ParticleTypes.cppm
            ParticleStreams.cppm
            ParticleModules.cppm
            ParticleEffect.cppm
            ParticlesModule.cppm)

