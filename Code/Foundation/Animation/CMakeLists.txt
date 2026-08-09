# Foundation::Animation - `foundation.animation`, the skeletal-animation foundation
# (skeleton/pose/clip/sampler/player + animation graph). A faithful port of
# Sedulous.Animation; pure logic over Core math + containers, no renderer dep.
add_library(Animation STATIC)
add_library(Foundation::Animation ALIAS Animation)

target_link_libraries(Animation
    PUBLIC Foundation::Core Foundation::Policy)

target_sources(Animation
    PUBLIC
        FILE_SET animation_modules TYPE CXX_MODULES FILES
            Easing.cppm
            Skeleton.cppm
            AnimationPose.cppm
            AnimationClip.cppm
            Sampler.cppm
            AnimationPlayer.cppm
            AnimationGraph.cppm
            AnimationModule.cppm)

# NB: the scene-integration lib (Animation/Subsystem) is added from the ROOT CMakeLists AFTER
# Render, because it links Engine::Render (which doesn't exist this early in config).
