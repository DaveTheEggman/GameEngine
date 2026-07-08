# Draconic::ParticlesResource - `draconic.particles.resource`: the cooked ParticleEffectResource
# (runtime input), its bidirectional serializer, and its resource factory. Layers on the runtime
# particle types + the content DB + resource manager. See docs/design/particles-authoring.md.
add_library(DraconicParticlesResource STATIC)
add_library(Draconic::ParticlesResource ALIAS DraconicParticlesResource)

target_link_libraries(DraconicParticlesResource
    PUBLIC Draconic::Core Draconic::Particles Draconic::Content Draconic::Resource
           Draconic::Texture Draconic::TextureResource Draconic::Policy)

target_sources(DraconicParticlesResource
    PUBLIC
        FILE_SET particles_resource_modules TYPE CXX_MODULES FILES
            ParticleEffectResource.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
