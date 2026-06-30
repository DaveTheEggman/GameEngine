/// Raptor::AnimationSubsystem — `raptor.animation.subsystem`, the scene side of skeletal animation.
///
/// SkeletalAnimationComponent (single clip) + AnimationGraphComponent (state machine / blend trees),
/// their managers (the animation<->render seam), and the AnimationSubsystem that injects the managers
/// into scenes. Depends on raptor.animation (foundation) + raptor.scene + raptor.render.subsystem
/// (the MeshComponent feed target); none of those depend back on it.

export module raptor.animation.subsystem;

export import :components;
export import :subsystem;
