# Raptor::AnimationSubsystem — `raptor.animation.subsystem`, the scene side of skeletal
# animation (SkeletalAnimationComponent + manager + AnimationSubsystem). Sits at the
# animation<->render seam: depends on Animation + Scene + RenderSubsystem (one-way).
add_library(RaptorAnimationSubsystem STATIC)
add_library(Raptor::AnimationSubsystem ALIAS RaptorAnimationSubsystem)

target_link_libraries(RaptorAnimationSubsystem
    PUBLIC Raptor::Core Raptor::Runtime Raptor::Scene Raptor::SceneSubsystem
           Raptor::Animation Raptor::RenderSubsystem Raptor::Policy)

target_sources(RaptorAnimationSubsystem
    PUBLIC
        FILE_SET animation_subsystem_modules TYPE CXX_MODULES FILES
            Components.cppm
            Subsystem.cppm
            AnimationSubsystemModule.cppm)
