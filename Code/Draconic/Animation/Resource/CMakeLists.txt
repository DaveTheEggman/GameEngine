# Draconic::AnimationResource — `draconic.animation.resource`: skeletons + clips as
# cooked resources (Source -> Factory -> runtime Skeleton/AnimationClip).
add_library(DraconicAnimationResource STATIC)
add_library(Draconic::AnimationResource ALIAS DraconicAnimationResource)

target_link_libraries(DraconicAnimationResource
    PUBLIC Draconic::Core Draconic::Resource Draconic::Content Draconic::Animation Draconic::Policy)

target_sources(DraconicAnimationResource
    PUBLIC
        FILE_SET animation_resource_modules TYPE CXX_MODULES FILES
            AnimationResource.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
