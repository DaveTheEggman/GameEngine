# Draconic::Particles.Pipeline - `draconic.particles.editor`: the edit-time ParticleEffectAsset + its
# builder (the bake). Tooling only - the runtime/app never links this. See
# docs/design/particles-authoring.md.
add_library(Draconic.Particles.Pipeline STATIC)
add_library(Draconic::Particles.Pipeline ALIAS Draconic.Particles.Pipeline)

target_link_libraries(Draconic.Particles.Pipeline
    PUBLIC Draconic::Core Draconic::Pipeline.Core Draconic::Particles Draconic::Particles.Resource
           Draconic::Content Draconic::Policy)

target_sources(Draconic.Particles.Pipeline
    PUBLIC
        FILE_SET particles_editor_modules TYPE CXX_MODULES FILES
            ParticleEffectAsset.cppm)

