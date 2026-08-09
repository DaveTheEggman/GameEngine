# Foundation::Animation.Resource - `draconic.animation.resource`: skeletons + clips as
# cooked resources (Source -> Factory -> runtime Skeleton/AnimationClip).
add_library(Animation.Resource STATIC)
add_library(Foundation::Animation.Resource ALIAS Animation.Resource)

target_link_libraries(Animation.Resource
    PUBLIC Foundation::Core Foundation::Resource Foundation::Content Foundation::Animation Foundation::Policy)

target_sources(Animation.Resource
    PUBLIC
        FILE_SET animation_resource_modules TYPE CXX_MODULES FILES
            AnimationResource.cppm)

