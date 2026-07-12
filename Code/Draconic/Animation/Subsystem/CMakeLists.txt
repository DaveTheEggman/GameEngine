# Draconic::AnimationSubsystem - `draconic.animation.subsystem`, the scene side of skeletal
# animation (SkeletalAnimationComponent + manager + AnimationSubsystem). Sits at the
# animation<->render seam: depends on Animation + Scene + RenderSubsystem (one-way).
add_library(DraconicAnimationSubsystem STATIC)
add_library(Draconic::AnimationSubsystem ALIAS DraconicAnimationSubsystem)

target_link_libraries(DraconicAnimationSubsystem
    PUBLIC Draconic::Core Draconic::Runtime Draconic::Scene Draconic::SceneSubsystem
           Draconic::Animation Draconic::Resource Draconic::RenderSubsystem Draconic::Policy)

target_sources(DraconicAnimationSubsystem
    PUBLIC
        FILE_SET animation_subsystem_modules TYPE CXX_MODULES FILES
            Components.cppm
            Subsystem.cppm
            AnimationSubsystemModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
