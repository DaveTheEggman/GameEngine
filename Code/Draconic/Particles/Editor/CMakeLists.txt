# Draconic::ParticlesEditor - `draconic.particles.editor`: the edit-time ParticleEffectAsset + its
# builder (the bake). Tooling only - the runtime/app never links this. See
# docs/design/particles-authoring.md.
add_library(DraconicParticlesEditor STATIC)
add_library(Draconic::ParticlesEditor ALIAS DraconicParticlesEditor)

target_link_libraries(DraconicParticlesEditor
    PUBLIC Draconic::Core Draconic::Editor Draconic::Particles Draconic::ParticlesResource
           Draconic::Content Draconic::Policy)

target_sources(DraconicParticlesEditor
    PUBLIC
        FILE_SET particles_editor_modules TYPE CXX_MODULES FILES
            ParticleEffectAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
