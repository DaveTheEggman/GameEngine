# Pipeline::Particles - `particles.pipeline`: the edit-time ParticleEffectAsset + its
# builder (the bake). Tooling only - the runtime/app never links this.
add_library(Particles.Pipeline STATIC)
add_library(Pipeline::Particles ALIAS Particles.Pipeline)

target_link_libraries(Particles.Pipeline
    PUBLIC Foundation::Core Pipeline::Core Foundation::Particles Foundation::Particles.Resource
           Foundation::Content Foundation::Policy)

target_sources(Particles.Pipeline
    PUBLIC
        FILE_SET particles_editor_modules TYPE CXX_MODULES FILES
            ParticleEffectAsset.cppm)

