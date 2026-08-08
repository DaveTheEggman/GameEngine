/// Draconic::AnimationSubsystem - `draconic.engine.animation`, the scene side of skeletal animation.
///
/// SkeletalAnimationComponent (single clip) + AnimationGraphComponent (state machine / blend trees),
/// their managers (the animation<->render seam), and the AnimationSubsystem that injects the managers
/// into scenes. Depends on draconic.animation (foundation) + draconic.scene + draconic.engine.render
/// (the MeshComponent feed target); none of those depend back on it.

export module draconic.engine.animation;

export import :components;
export import :subsystem;
