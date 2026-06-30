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
            Clip.cppm
            Sampler.cppm
            Player.cppm
            Graph.cppm
            AnimationModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Skeletons + clips as cooked resources (Source -> Factory -> runtime types).
add_subdirectory(Resource)

# Authoring + cook (tooling: SkeletonAsset/AnimationClipAsset -> source).
add_subdirectory(Editor)

# NB: the scene-integration lib (Animation/Subsystem) is added from the ROOT CMakeLists AFTER
# Render, because it links Raptor::RenderSubsystem (which doesn't exist this early in config).
