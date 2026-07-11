/// Draconic::AnimationSubsystem - the `:components` partition.
///
/// The scene-facing side of skeletal animation. Two components, each with a manager that ticks its
/// players every frame and feeds the resulting skinning matrices into the target MeshComponent(s)
/// for GPU skinning: SkeletalAnimationComponent (a single clip via AnimationPlayer) and
/// AnimationGraphComponent (a state machine / blend trees via AnimationGraphPlayer). This is what
/// replaces driving players by hand in app code - the engine now animates skinned meshes from the
/// scene tick.
///
/// It sits at the animation<->render seam (depends on both draconic.animation and the render
/// components); neither of those depends back on it.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.animation.subsystem:components;

import draconic.core;
import draconic.scene;
import draconic.animation;          // Skeleton, AnimationClip, AnimationPlayer, AnimationGraph(+Player)
import draconic.render.subsystem;   // MeshComponentManager / MeshComponent (the feed target)

using namespace draconic::core;
namespace scene   = draconic::scene;
namespace animation = draconic::animation;

export namespace draconic::animation {

// Skeletal animation on an entity: a player over a (borrowed, shared) skeleton plays a clip and
// produces per-bone skinning matrices each frame. The manager owns the player's lifetime + tick.
// `meshEntities` are the entities whose MeshComponent receives the matrices (a character's skinned
// mesh nodes); empty => feed the component's own entity. All borrowed resources must outlive the
// component (the resource manager / model keeps the skeleton + clip alive).
struct SkeletalAnimationComponent {
    animation::Skeleton*                  skeleton = nullptr;   // borrowed; shared across instances
    animation::AnimationClip*             clip     = nullptr;   // borrowed; the clip to play (autoPlay)
    UniquePtr<animation::AnimationPlayer> player;               // created lazily by the manager
    Array<scene::EntityHandle>          meshEntities;         // feed targets (empty => own entity)
    f32                              speed     = 1.0f;
    f32                              startTime = 0.0f;     // initial clock (desync a herd); applied on first tick
    bool                             autoPlay  = true;     // Play(clip) on first tick
};

// Ticks every SkeletalAnimationComponent in ScenePhase::PostUpdate (the "animation" phase, before
// render extraction): advance each player, then write its current + previous skinning matrices into
// the target MeshComponent(s) (borrowed for the frame - the player, owned by the component, keeps
// the matrix storage alive). Lazily creates each component's player on first tick.
class SkeletalAnimationComponentManager final : public scene::ComponentManager<SkeletalAnimationComponent> {
public:
    void OnSceneCreate(scene::Scene& scene) override { m_scene = &scene; }

    // Animation is gameplay-side state; only advance it while the scene is simulating? Keep it
    // always-on for now so apps animate without an explicit Start() (revisit with edit-mode).
    [[nodiscard]] bool IsSimulationOnly() const noexcept override { return false; }

    void OnUpdate(scene::ScenePhase phase, f32 deltaTime) override {
        if (phase != scene::ScenePhase::PostUpdate || m_scene == nullptr) { return; }
        auto* meshes = m_scene->GetSystem<render::MeshComponentManager>();
        if (meshes == nullptr) { return; }

        ForEach([&](SkeletalAnimationComponent& a, scene::EntityHandle owner) {
            if (a.skeleton == nullptr) { return; }
            if (a.player.Get() == nullptr) {
                a.player = MakeUnique<animation::AnimationPlayer>(DefaultAllocator(), *a.skeleton);
                if (a.autoPlay && a.clip != nullptr) {
                    a.player->Play(a.clip);
                    if (a.startTime != 0.0f) { a.player->SetCurrentTime(a.startTime); }
                }
            }
            a.player->speed = a.speed;
            a.player->Update(deltaTime);
            const Span<const Float4x4> mats = a.player->GetSkinningMatrices();
            const Span<const Float4x4> prev = a.player->GetPrevSkinningMatrices();
            const auto feed = [&](scene::EntityHandle e) {
                if (render::MeshComponent* mc = meshes->Get(e)) {
                    mc->boneMatrices     = mats.Data();
                    mc->prevBoneMatrices = prev.Data();
                    mc->boneCount        = static_cast<u32>(mats.Size());
                }
            };
            if (a.meshEntities.IsEmpty()) { feed(owner); }
            else { for (scene::EntityHandle e : a.meshEntities) { feed(e); } }
        });
    }

private:
    scene::Scene* m_scene = nullptr;
};

// State-machine-driven skeletal animation: a graph player (over a borrowed, shared skeleton +
// AnimationGraph) evaluates the graph each frame - state transitions, blend trees, layer blending -
// and produces per-bone skinning matrices. The richer counterpart to SkeletalAnimationComponent
// (single clip); drive transitions via the player's parameters (SetFloat/SetBool/SetTrigger). Same
// feed contract: `meshEntities` are the MeshComponents that receive the matrices (empty => own
// entity). All borrowed resources must outlive the component.
struct AnimationGraphComponent {
    animation::Skeleton*                       skeleton = nullptr;  // borrowed; shared across instances
    animation::AnimationGraph*                 graph    = nullptr;  // borrowed; the state machine to evaluate
    UniquePtr<animation::AnimationGraphPlayer> player;              // created lazily by the manager
    Array<scene::EntityHandle>               meshEntities;        // feed targets (empty => own entity)
    bool                                  active   = true;     // evaluate this frame?
};

// Ticks every AnimationGraphComponent in ScenePhase::PostUpdate, same as the skeletal manager but
// evaluating an AnimationGraphPlayer. Runs at a LOWER UpdateOrder (before SkeletalAnimationComponent-
// Manager), mirroring Sedulous's graph-before-clip ordering; an entity is expected to use one or the
// other (mixing both pushes to the same MeshComponent - the later writer wins).
class AnimationGraphComponentManager final : public scene::ComponentManager<AnimationGraphComponent> {
public:
    void OnSceneCreate(scene::Scene& scene) override { m_scene = &scene; }

    [[nodiscard]] bool IsSimulationOnly() const noexcept override { return false; }

    // Run before the simple-clip manager (UpdateOrder 0) so the graph drives graph-backed entities.
    [[nodiscard]] i32 UpdateOrder() const noexcept override { return -1; }

    void OnUpdate(scene::ScenePhase phase, f32 deltaTime) override {
        if (phase != scene::ScenePhase::PostUpdate || m_scene == nullptr) { return; }
        auto* meshes = m_scene->GetSystem<render::MeshComponentManager>();
        if (meshes == nullptr) { return; }

        ForEach([&](AnimationGraphComponent& a, scene::EntityHandle owner) {
            if (a.skeleton == nullptr || a.graph == nullptr) { return; }
            if (a.player.Get() == nullptr) {
                a.player = MakeUnique<animation::AnimationGraphPlayer>(DefaultAllocator(), *a.graph, *a.skeleton);
            }
            if (!a.active) { return; }
            a.player->Update(deltaTime);
            const Span<const Float4x4> mats = a.player->GetSkinningMatrices();
            const Span<const Float4x4> prev = a.player->GetPrevSkinningMatrices();
            const auto feed = [&](scene::EntityHandle e) {
                if (render::MeshComponent* mc = meshes->Get(e)) {
                    mc->boneMatrices     = mats.Data();
                    mc->prevBoneMatrices = prev.Data();
                    mc->boneCount        = static_cast<u32>(mats.Size());
                }
            };
            if (a.meshEntities.IsEmpty()) { feed(owner); }
            else { for (scene::EntityHandle e : a.meshEntities) { feed(e); } }
        });
    }

private:
    scene::Scene* m_scene = nullptr;
};

// Instanced skinning for CROWDS: the companion to a render::InstancedMeshComponent (a "MultiMesh") that
// makes its N instances animate at only M = poseCount unique phases. Each frame the manager samples the
// clip at M evenly-spaced phases (advancing together on a shared clock) into a shared POSE POOL of M
// skinning palettes, and feeds the pool to the target InstancedMeshComponent - which draws instance i
// with pose (i % M). So a 30k crowd costs M palette computes, not 30k. Put it on the same entity as the
// InstancedMeshComponent (empty target) or point `target` at it. Borrowed skeleton/clip must outlive it.
// See docs/design/instanced-mesh.md SS7.
struct InstancedSkinning {
    animation::Skeleton*      skeleton  = nullptr;   // borrowed; shared across the crowd
    animation::AnimationClip* clip      = nullptr;   // borrowed; the clip the crowd plays
    u32                       poseCount = 32;        // M unique phase buckets (more = smoother spread, more compute)
    f32                       speed     = 1.0f;
    Array<scene::EntityHandle> targets;              // InstancedMeshComponent entities to feed (a multi-part
                                                     // character = one set per skinned mesh); empty => own entity

    // Manager-owned per-frame state (not authored).
    Array<Float4x4>          posePool;              // poseCount * boneCount skinning matrices, recomputed each frame
    Array<Float4x4>          prevPosePool;          // LAST frame's palettes (per-bone motion vectors); ping-ponged, not recomputed
    Array<BoneTransform>    scratch;               // boneCount scratch for SampleClip
    f32                        time      = 0.0f;      // shared clock (wrapped to clip duration)
    u32                        boneCount = 0;
};

// Ticks every InstancedSkinning in PostUpdate (before render extraction): advance the shared clock, sample
// the clip at M phases into the pose pool, and hand the pool to the target InstancedMeshComponent.
class InstancedSkinningManager final : public scene::ComponentManager<InstancedSkinning> {
public:
    void OnSceneCreate(scene::Scene& scene) override { m_scene = &scene; }
    [[nodiscard]] bool IsSimulationOnly() const noexcept override { return false; }

    void OnUpdate(scene::ScenePhase phase, f32 deltaTime) override {
        if (phase != scene::ScenePhase::PostUpdate || m_scene == nullptr) { return; }
        auto* imm = m_scene->GetSystem<render::InstancedMeshComponentManager>();
        if (imm == nullptr) { return; }

        ForEach([&](InstancedSkinning& s, scene::EntityHandle owner) {
            if (s.skeleton == nullptr || s.clip == nullptr || s.poseCount == 0) { return; }
            const u32 boneCount = static_cast<u32>(s.skeleton->BoneCount());
            if (boneCount == 0) { return; }
            s.boneCount = boneCount;
            const usize poolSize = static_cast<usize>(s.poseCount) * boneCount;

            // Ping-pong: last frame's pool becomes this frame's PREV (per-bone motion vectors) - no re-sampling.
            { Array<Float4x4> tmp = static_cast<Array<Float4x4>&&>(s.posePool);
              s.posePool = static_cast<Array<Float4x4>&&>(s.prevPosePool);
              s.prevPosePool = static_cast<Array<Float4x4>&&>(tmp); }
            s.posePool.Resize(poolSize);
            s.scratch.Resize(boneCount);

            const f32 duration = (s.clip->duration > 0.0f) ? s.clip->duration : 1.0f;
            s.time += deltaTime * s.speed;
            while (s.time >= duration) { s.time -= duration; }
            while (s.time < 0.0f)      { s.time += duration; }

            // M palettes at M evenly-spaced phases (the whole crowd cycles through the clip together).
            for (u32 m = 0; m < s.poseCount; ++m) {
                f32 p = s.time + (static_cast<f32>(m) / static_cast<f32>(s.poseCount)) * duration;
                while (p >= duration) { p -= duration; }
                SampleClip(*s.clip, *s.skeleton, p, Span<BoneTransform>{ s.scratch.Data(), s.scratch.Size() });
                s.skeleton->ComputeSkinningMatrices(
                    Span<const BoneTransform>{ s.scratch.Data(), s.scratch.Size() },
                    Span<Float4x4>{ s.posePool.Data() + static_cast<usize>(m) * boneCount, boneCount });
            }
            // First frame (or pose-count change): no prev yet -> prev = current (zero motion).
            if (s.prevPosePool.Size() != poolSize) {
                s.prevPosePool.Resize(poolSize);
                if (poolSize > 0) { MemCopy(s.prevPosePool.Data(), s.posePool.Data(), poolSize * sizeof(Float4x4)); }
            }

            const auto feed = [&](scene::EntityHandle e) {
                if (render::InstancedMeshComponent* c = imm->Get(e)) {
                    c->posePool     = s.posePool.Data();       // borrowed for the frame (the component keeps the storage alive)
                    c->prevPosePool = s.prevPosePool.Data();   // last frame's palettes (per-bone motion vectors)
                    c->poseCount    = s.poseCount;
                    c->boneCount    = boneCount;
                }
            };
            if (s.targets.IsEmpty()) { feed(owner); }
            else { for (scene::EntityHandle e : s.targets) { feed(e); } }
        });
    }

private:
    scene::Scene* m_scene = nullptr;
};


} // exported namespace

// Reflection (tooling: the editor inspector; pointer/player/array fields are deliberately not
// reflected - they need resource pickers). NON-export namespace: the macros expand static
// helpers (CoreReflection.cppm pattern).
namespace draconic::animation
{

DRACONIC_REFLECT_VALUE(SkeletalAnimationComponent, "draconic::animation")
{
    builder.Property<&SkeletalAnimationComponent::speed>("speed")
           .Property<&SkeletalAnimationComponent::startTime>("startTime")
           .Property<&SkeletalAnimationComponent::autoPlay>("autoPlay");
}

DRACONIC_REFLECT_VALUE(AnimationGraphComponent, "draconic::animation")
{
    builder.Property<&AnimationGraphComponent::active>("active");
}

DRACONIC_REFLECT_VALUE(InstancedSkinning, "draconic::animation")
{
    builder.Property<&InstancedSkinning::poseCount>("poseCount")
           .Property<&InstancedSkinning::speed>("speed");
}

} // namespace draconic::animation (reflection bodies)

export namespace draconic::animation
{
    void RegisterAnimationComponentReflection();
}

namespace draconic::animation
{
    void RegisterAnimationComponentReflection()
    {
        static const bool once = []() {
            DraconicRegisterValue_SkeletalAnimationComponent();
            DraconicRegisterValue_AnimationGraphComponent();
            DraconicRegisterValue_InstancedSkinning();
            return true;
        }();
        (void)once;
    }
}
