# Raptor::Animation — `raptor.animation`, the skeletal-animation foundation
# (skeleton/pose/clip/sampler/player + animation graph). A faithful port of
# Sedulous.Animation; pure logic over Core math + containers, no renderer dep.
add_library(RaptorAnimation STATIC)
add_library(Raptor::Animation ALIAS RaptorAnimation)

target_link_libraries(RaptorAnimation
    PUBLIC Raptor::Core Raptor::Policy)

target_sources(RaptorAnimation
    PUBLIC
        FILE_SET animation_modules TYPE CXX_MODULES FILES
            Easing.cppm
            Skeleton.cppm
            Pose.cppm
            AnimationModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
