# Draconic::Animation - `draconic.animation`, the skeletal-animation foundation
# (skeleton/pose/clip/sampler/player + animation graph). A faithful port of
# Sedulous.Animation; pure logic over Core math + containers, no renderer dep.
add_library(DraconicAnimation STATIC)
add_library(Draconic::Animation ALIAS DraconicAnimation)

target_link_libraries(DraconicAnimation
    PUBLIC Draconic::Core Draconic::Policy)

target_sources(DraconicAnimation
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

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Skeletons + clips as cooked resources (Source -> Factory -> runtime types).
add_subdirectory(Resource)

# Authoring + cook (tooling: SkeletonAsset/AnimationClipAsset -> source).
if(DRACONIC_BUILD_TOOLS)
    add_subdirectory(Editor)
endif()

# NB: the scene-integration lib (Animation/Subsystem) is added from the ROOT CMakeLists AFTER
# Render, because it links Draconic::RenderSubsystem (which doesn't exist this early in config).
