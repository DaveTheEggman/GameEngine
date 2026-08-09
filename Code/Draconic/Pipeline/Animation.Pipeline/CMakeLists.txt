# Draconic::Animation.Pipeline - `draconic.animation.editor`: authoring assets + builders
# that cook skeletons/clips into the content DB. Tooling only (runtime loads cooked).
add_library(Draconic.Animation.Pipeline STATIC)
add_library(Draconic::Animation.Pipeline ALIAS Draconic.Animation.Pipeline)

target_link_libraries(Draconic.Animation.Pipeline
    PUBLIC Draconic::Core Draconic::Pipeline.Core Draconic::Content
           Draconic::Animation Draconic::Animation.Resource Draconic::Policy)

target_sources(Draconic.Animation.Pipeline
    PUBLIC
        FILE_SET animation_editor_modules TYPE CXX_MODULES FILES
            AnimationAsset.cppm)

