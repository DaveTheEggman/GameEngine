# Pipeline::Animation - `draconic.animation.editor`: authoring assets + builders
# that cook skeletons/clips into the content DB. Tooling only (runtime loads cooked).
add_library(Animation.Pipeline STATIC)
add_library(Pipeline::Animation ALIAS Animation.Pipeline)

target_link_libraries(Animation.Pipeline
    PUBLIC Foundation::Core Pipeline::Core Foundation::Content
           Foundation::Animation Foundation::Animation.Resource Foundation::Policy)

target_sources(Animation.Pipeline
    PUBLIC
        FILE_SET animation_editor_modules TYPE CXX_MODULES FILES
            AnimationAsset.cppm)

