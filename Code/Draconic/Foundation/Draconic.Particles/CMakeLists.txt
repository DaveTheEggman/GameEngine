# Draconic::Particles - `draconic.particles`, the CPU particle runtime (a port of
# SedulousEngine's Sedulous.Particles): SoA stream container + initializer/behavior
# modules + effect/system/emitter/instance + CPU simulator. Pure logic over Core; the
# cooked resource (.resource), the GPU-compute sim, the ECS/render integration, and the
# authoring editor all layer on top. See docs/design/particles.md.
add_library(Draconic.Particles STATIC)
add_library(Draconic::Particles ALIAS Draconic.Particles)

target_link_libraries(Draconic.Particles
    PUBLIC Draconic::Core Draconic::Policy)

target_sources(Draconic.Particles
    PUBLIC
        FILE_SET particles_modules TYPE CXX_MODULES FILES
            ParticleTypes.cppm
            ParticleStreams.cppm
            ParticleModules.cppm
            ParticleEffect.cppm
            ParticlesModule.cppm)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Particles.Resource ${CMAKE_CURRENT_BINARY_DIR}/Resource)   # draconic.particles.resource (cooked ParticleEffectResource + factory)
if(DRACONIC_BUILD_TOOLS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../Editor/Draconic.Particles.Editor ${CMAKE_CURRENT_BINARY_DIR}/Editor)     # draconic.particles.editor  (ParticleEffectAsset + bake; tooling only)
endif()

