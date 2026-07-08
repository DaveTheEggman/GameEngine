# Draconic::Particles - `draconic.particles`, the CPU particle runtime (a port of
# SedulousEngine's Sedulous.Particles): SoA stream container + initializer/behavior
# modules + effect/system/emitter/instance + CPU simulator. Pure logic over Core; the
# cooked resource (.resource), the GPU-compute sim, the ECS/render integration, and the
# authoring editor all layer on top. See docs/design/particles.md.
add_library(DraconicParticles STATIC)
add_library(Draconic::Particles ALIAS DraconicParticles)

target_link_libraries(DraconicParticles
    PUBLIC Draconic::Core Draconic::Policy)

target_sources(DraconicParticles
    PUBLIC
        FILE_SET particles_modules TYPE CXX_MODULES FILES
            Types.cppm
            Streams.cppm
            Modules.cppm
            Effect.cppm
            ParticlesModule.cppm)

add_subdirectory(Resource)   # draconic.particles.resource (cooked ParticleEffectResource + factory)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
