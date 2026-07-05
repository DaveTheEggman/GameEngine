/// Draconic::AnimationSubsystem — the `:components` partition.
///
/// The scene-facing side of skeletal animation. Two components, each with a manager that ticks its
/// players every frame and feeds the resulting skinning matrices into the target MeshComponent(s)
/// for GPU skinning: SkeletalAnimationComponent (a single clip via AnimationPlayer) and
/// AnimationGraphComponent (a state machine / blend trees via AnimationGraphPlayer). This is what
/// replaces driving players by hand in app code — the engine now animates skinned meshes from the
/// scene tick.
///
/// It sits at the animation<->render seam (depends on both draconic.animation and the render
/// components); neither of those depends back on it.

module;
#include "Core/Prelude.h"

export module draconic.animation.subsystem:components;

import draconic.core;
import draconic.scene;
import draconic.animation;          // Skeleton, AnimationClip, AnimationPlayer, AnimationGraph(+Player)
import draconic.render.subsystem;   // MeshComponentManager / MeshComponent (the feed target)

using namespace draconic::core;
namespace sc   = draconic::scene;
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
    Array<sc::EntityHandle>          meshEntities;         // feed targets (empty => own entity)
    f32                              speed     = 1.0f;
    f32                              startTime = 0.0f;     // initial clock (desync a herd); applied on first tick
    bool                             autoPlay  = true;     // Play(clip) on first tick
};

// Ticks every SkeletalAnimationComponent in ScenePhase::PostUpdate (the "animation" phase, before
// render extraction): advance each player, then write its current + previous skinning matrices into
// the target MeshComponent(s) (borrowed for the frame — the player, owned by the component, keeps
// the matrix storage alive). Lazily creates each component's player on first tick.
class SkeletalAnimationComponentManager final : public sc::ComponentManager<SkeletalAnimationComponent> {
public:
    void OnSceneCreate(sc::Scene& scene) override { m_scene = &scene; }

    // Animation is gameplay-side state; only advance it while the scene is simulating? Keep it
    // always-on for now so apps animate without an explicit Start() (revisit with edit-mode).
    [[nodiscard]] bool IsSimulationOnly() const noexcept override { return false; }

    void OnUpdate(sc::ScenePhase phase, f32 deltaTime) override {
        if (phase != sc::ScenePhase::PostUpdate || m_scene == nullptr) { return; }
        auto* meshes = m_scene->GetSystem<render::MeshComponentManager>();
        if (meshes == nullptr) { return; }

        ForEach([&](SkeletalAnimationComponent& a, sc::EntityHandle owner) {
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
            const Span<const Matrix4> mats = a.player->GetSkinningMatrices();
            const Span<const Matrix4> prev = a.player->GetPrevSkinningMatrices();
            const auto feed = [&](sc::EntityHandle e) {
                if (render::MeshComponent* mc = meshes->Get(e)) {
                    mc->boneMatrices     = mats.Data();
                    mc->prevBoneMatrices = prev.Data();
                    mc->boneCount        = static_cast<u32>(mats.Size());
                }
            };
            if (a.meshEntities.IsEmpty()) { feed(owner); }
            else { for (sc::EntityHandle e : a.meshEntities) { feed(e); } }
        });
    }

private:
    sc::Scene* m_scene = nullptr;
};

// State-machine-driven skeletal animation: a graph player (over a borrowed, shared skeleton +
// AnimationGraph) evaluates the graph each frame — state transitions, blend trees, layer blending —
// and produces per-bone skinning matrices. The richer counterpart to SkeletalAnimationComponent
// (single clip); drive transitions via the player's parameters (SetFloat/SetBool/SetTrigger). Same
// feed contract: `meshEntities` are the MeshComponents that receive the matrices (empty => own
// entity). All borrowed resources must outlive the component.
struct AnimationGraphComponent {
    animation::Skeleton*                       skeleton = nullptr;  // borrowed; shared across instances
    animation::AnimationGraph*                 graph    = nullptr;  // borrowed; the state machine to evaluate
    UniquePtr<animation::AnimationGraphPlayer> player;              // created lazily by the manager
    Array<sc::EntityHandle>               meshEntities;        // feed targets (empty => own entity)
    bool                                  active   = true;     // evaluate this frame?
};

// Ticks every AnimationGraphComponent in ScenePhase::PostUpdate, same as the skeletal manager but
// evaluating an AnimationGraphPlayer. Runs at a LOWER UpdateOrder (before SkeletalAnimationComponent-
// Manager), mirroring Sedulous's graph-before-clip ordering; an entity is expected to use one or the
// other (mixing both pushes to the same MeshComponent — the later writer wins).
class AnimationGraphComponentManager final : public sc::ComponentManager<AnimationGraphComponent> {
public:
    void OnSceneCreate(sc::Scene& scene) override { m_scene = &scene; }

    [[nodiscard]] bool IsSimulationOnly() const noexcept override { return false; }

    // Run before the simple-clip manager (UpdateOrder 0) so the graph drives graph-backed entities.
    [[nodiscard]] i32 UpdateOrder() const noexcept override { return -1; }

    void OnUpdate(sc::ScenePhase phase, f32 deltaTime) override {
        if (phase != sc::ScenePhase::PostUpdate || m_scene == nullptr) { return; }
        auto* meshes = m_scene->GetSystem<render::MeshComponentManager>();
        if (meshes == nullptr) { return; }

        ForEach([&](AnimationGraphComponent& a, sc::EntityHandle owner) {
            if (a.skeleton == nullptr || a.graph == nullptr) { return; }
            if (a.player.Get() == nullptr) {
                a.player = MakeUnique<animation::AnimationGraphPlayer>(DefaultAllocator(), *a.graph, *a.skeleton);
            }
            if (!a.active) { return; }
            a.player->Update(deltaTime);
            const Span<const Matrix4> mats = a.player->GetSkinningMatrices();
            const Span<const Matrix4> prev = a.player->GetPrevSkinningMatrices();
            const auto feed = [&](sc::EntityHandle e) {
                if (render::MeshComponent* mc = meshes->Get(e)) {
                    mc->boneMatrices     = mats.Data();
                    mc->prevBoneMatrices = prev.Data();
                    mc->boneCount        = static_cast<u32>(mats.Size());
                }
            };
            if (a.meshEntities.IsEmpty()) { feed(owner); }
            else { for (sc::EntityHandle e : a.meshEntities) { feed(e); } }
        });
    }

private:
    sc::Scene* m_scene = nullptr;
};

} // namespace draconic::animation
