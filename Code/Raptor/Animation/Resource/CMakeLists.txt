# Raptor::AnimationResource — `raptor.animation.resource`: skeletons + clips as
# cooked resources (Source -> Factory -> runtime Skeleton/AnimationClip).
add_library(RaptorAnimationResource STATIC)
add_library(Raptor::AnimationResource ALIAS RaptorAnimationResource)

target_link_libraries(RaptorAnimationResource
    PUBLIC Raptor::Core Raptor::Resource Raptor::Content Raptor::Animation Raptor::Policy)

target_sources(RaptorAnimationResource
    PUBLIC
        FILE_SET animation_resource_modules TYPE CXX_MODULES FILES
            AnimationResource.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
