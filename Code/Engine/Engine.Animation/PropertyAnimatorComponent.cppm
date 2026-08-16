// engine.animation:propertyanimator
//
// PropertyAnimatorComponent + its per-scene manager (property-animation.md Engine side). One animator
// per entity plays ONE clip whose tracks drive reflected properties on the OWNING entity's components.
// The manager ticks in PostUpdate (before render extraction, the skeletal phase): advance the play
// clock (Once/Loop/PingPong), then for each track re-resolve the LIVE target component via the scene's
// component-by-type lookup (never a cached pointer - the sparse-set swap-remove lesson) and write the
// sampled Variant through the reflection binding. A track that fails to resolve is disabled with ONE
// warning; the clip keeps playing its other tracks.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module engine.animation:propertyanimator;

import foundation.core;
import foundation.resource;
import foundation.scene;
import foundation.propertyanimation;
import foundation.propertyanimation.resource;

using namespace foundation::core;

export namespace engine::animation
{
    namespace scene = foundation::scene;
    namespace propanim = foundation::propertyanimation;

    enum class PropertyLoopMode : u8
    {
        Once,     // play through once, then stop
        Loop,     // wrap to the start
        PingPong, // bounce between ends
    };

    // Per-track runtime binding: the target component's manager (found by type name, stable for the
    // scene) + the resolved PropertyInfo chain (stable static metadata). The component INSTANCE is
    // re-resolved every write via the manager - never cached here. `disabled` = a failed resolve
    // (warned once); the track is skipped, the rest of the clip plays on.
    struct PropertyTrackBinding
    {
        scene::ComponentManagerBase* manager = nullptr; // null for the built-in Transform target
        propanim::PropertyBinding binding;
        bool disabled = false;
        bool isTransform = false; // targets the entity's scene transform (no component manager)
    };

    // The entity's local transform (position/rotation/scale) is not a reflected COMPONENT - it is
    // baked into the scene - so tracks name it with this reserved component type and the manager
    // binds it to core::Transform, writing through Set/GetLocalTransform (which flags the world
    // matrix dirty). Reserved: a real component may not take this name.
    inline constexpr StringView kTransformComponentName = u8"Transform";

    // PHYSICS RULE (property-animation.md): the animator writes the transform like any other property.
    // On an entity with a DYNAMIC rigid body, physics is authoritative - its per-frame pose sync
    // overwrites the animated transform (they run in different scene phases and the body integrates
    // independently). Animate the transform only of KINEMATIC (or non-physics) entities; a dynamic
    // body's non-transform properties animate fine. (The cross-subsystem test proving this rides the
    // physics manager - a follow-up beyond this engine module's own suite.)

    // Plays one property-animation clip on the owning entity. Serialized fields = the clip ref +
    // playback tunables; the play clock, bindings, and cached clip are runtime-only.
    struct PropertyAnimatorComponent
    {
        foundation::resource::Ref<propanim::PropertyAnimationClipResource> clip;
        bool autoplay = true;
        f32 speed = 1.0f;
        PropertyLoopMode loopMode = PropertyLoopMode::Loop;

        // runtime state
        bool playing = false;
        f32 time = 0.0f;
        i8 pingPongDir = 1;
        propanim::PropertyAnimationClipResource* boundClip = nullptr; // bindings built for this clip
        Array<PropertyTrackBinding> bindings;

        // Reflected playback ops for scripting (property-animation.md: NO new facade lib - these reach
        // scripts through the existing reflected-component `.of(entity)` surface, natural types per the
        // facade-numerics rule). They mutate only the runtime clock/state; the manager tick applies it.
        void play()
        {
            playing = true;
            time = 0.0f;
            pingPongDir = 1;
        }
        void stop()
        {
            playing = false;
            time = 0.0f;
        }
        void pause() { playing = false; }
        void resume() { playing = true; }
        [[nodiscard]] bool isPlaying() const { return playing; }
        [[nodiscard]] f32 currentTime() const { return time; }
        void setTime(f32 seconds) { time = seconds; }
    };

    inline void Serialize(ISerializer& ar, PropertyAnimatorComponent& c)
    {
        foundation::core::Serialize(ar, "clip", c.clip);
        foundation::core::Serialize(ar, "autoplay", c.autoplay);
        foundation::core::Serialize(ar, "speed", c.speed);
        foundation::core::Serialize(ar, "loopMode", c.loopMode);
    }

    inline void ResolveResources(foundation::resource::ResourceManager& manager,
                                 PropertyAnimatorComponent& c)
    {
        c.clip.Bind(manager);
    }

    class PropertyAnimatorComponentManager final
        : public scene::SerializableComponentManager<PropertyAnimatorComponent>
    {
    public:
        PropertyAnimatorComponentManager()
            : scene::SerializableComponentManager<PropertyAnimatorComponent>(u8"property_animator")
        {
        }

        void OnSceneCreate(scene::Scene& scene) override { m_scene = &scene; }

        // Property animation is gameplay-side but harmless in edit mode (it just writes props); keep it
        // always-on so a clip previews without an explicit Start (matches the skeletal manager).
        [[nodiscard]] bool IsSimulationOnly() const noexcept override { return false; }

        void OnUpdate(scene::ScenePhase phase, f32 deltaTime) override
        {
            if (phase != scene::ScenePhase::PostUpdate || m_scene == nullptr)
            {
                return;
            }
            ForEach(
                [&](PropertyAnimatorComponent& a, scene::EntityHandle owner)
                {
                    propanim::PropertyAnimationClipResource* clipRes = a.clip.Get();
                    if (clipRes == nullptr)
                    {
                        return;
                    }
                    // (Re)build the bindings when the clip object changes (first tick, editor pick,
                    // hot reload). autoplay starts the new clip from t=0.
                    if (clipRes != a.boundClip)
                    {
                        a.boundClip = clipRes;
                        BuildBindings(a, clipRes->clip, owner);
                        if (a.autoplay)
                        {
                            a.playing = true;
                            a.time = 0.0f;
                            a.pingPongDir = 1;
                        }
                    }
                    if (!a.playing)
                    {
                        return;
                    }

                    const propanim::PropertyAnimationClip& clip = clipRes->clip;
                    const f32 evalTime = Advance(a, clip.duration, deltaTime);

                    const usize trackCount =
                        Min(a.bindings.Size(), clip.tracks.Size());
                    for (usize i = 0; i < trackCount; ++i)
                    {
                        PropertyTrackBinding& tb = a.bindings[i];
                        if (tb.disabled || !tb.binding.IsResolved())
                        {
                            continue;
                        }
                        const propanim::PropertyTrack& track = clip.tracks[i];
                        if (tb.isTransform)
                        {
                            // Read-modify-write the scene transform: SetLocalTransform flags the
                            // world matrix dirty (a raw field write would not). SampleMerged keeps
                            // the live value for empty channels (no teleport-to-origin).
                            Transform local = m_scene->GetLocalTransform(owner);
                            const Instance inst{&local, &TypeOf<Transform>()};
                            const Variant value =
                                track.SampleMerged(evalTime, propanim::ReadBinding(tb.binding, inst));
                            if (propanim::WriteBinding(tb.binding, inst, value).IsOk())
                            {
                                m_scene->SetLocalTransform(owner, local);
                            }
                            continue;
                        }
                        if (tb.manager == nullptr)
                        {
                            continue;
                        }
                        // Re-resolve the LIVE component every write (structural-change safe).
                        const Instance inst = tb.manager->GetComponentInstance(owner);
                        if (inst.IsEmpty())
                        {
                            continue; // component removed this frame - skip, keep the binding
                        }
                        const Variant value =
                            track.SampleMerged(evalTime, propanim::ReadBinding(tb.binding, inst));
                        (void)propanim::WriteBinding(tb.binding, inst, value);
                    }
                });
        }

    private:
        // Resolve each track's target component manager (by type name) + property binding once. A
        // track whose component type or property path can't resolve is disabled with one warning.
        void BuildBindings(PropertyAnimatorComponent& a, const propanim::PropertyAnimationClip& clip,
                           scene::EntityHandle owner)
        {
            (void)owner;
            a.bindings.Clear();
            for (const propanim::PropertyTrack& track : clip.tracks)
            {
                PropertyTrackBinding tb;
                // Built-in Transform target: bind against core::Transform (no component manager).
                if (track.componentType.AsView() == kTransformComponentName)
                {
                    tb.isTransform = true;
                    tb.binding = propanim::ResolveBinding(TypeOf<Transform>(),
                                                          track.propertyPath.AsView());
                    if (!tb.binding.IsResolved())
                    {
                        LOG_WARNING(u8"PropertyAnimation",
                                    u8"track property 'Transform.{}' not found - disabled",
                                    track.propertyPath);
                        tb.disabled = true;
                    }
                    a.bindings.PushBack(Move(tb));
                    continue;
                }
                scene::ComponentManagerBase* manager =
                    FindManagerByComponentTypeName(track.componentType.AsView());
                if (manager == nullptr || manager->ComponentType() == nullptr)
                {
                    LOG_WARNING(u8"PropertyAnimation",
                                u8"track targets unknown component type '{}' - disabled",
                                track.componentType);
                    tb.disabled = true;
                    a.bindings.PushBack(Move(tb));
                    continue;
                }
                tb.manager = manager;
                tb.binding =
                    propanim::ResolveBinding(*manager->ComponentType(), track.propertyPath.AsView());
                if (!tb.binding.IsResolved())
                {
                    LOG_WARNING(u8"PropertyAnimation",
                                u8"track property '{}' not found on '{}' - disabled",
                                track.propertyPath, track.componentType);
                    tb.disabled = true;
                }
                a.bindings.PushBack(Move(tb));
            }
        }

        // The scene's component manager whose reflected component type is named `name`, or null.
        [[nodiscard]] scene::ComponentManagerBase* FindManagerByComponentTypeName(StringView name)
        {
            scene::ComponentManagerBase* found = nullptr;
            m_scene->ForEachManager(
                [&](scene::ComponentManagerBase& m)
                {
                    if (found == nullptr && m.ComponentType() != nullptr && m.ComponentType()->name &&
                        StringView(reinterpret_cast<const utf8char*>(m.ComponentType()->name)) == name)
                    {
                        found = &m;
                    }
                });
            return found;
        }

        // Advance the play clock by `dt` under the loop mode; returns the evaluation time and updates
        // playing/pingPongDir. `speed` may be negative.
        [[nodiscard]] static f32 Advance(PropertyAnimatorComponent& a, f32 duration, f32 dt)
        {
            if (duration <= 0.0f)
            {
                return 0.0f;
            }
            const f32 dir = (a.loopMode == PropertyLoopMode::PingPong)
                                ? static_cast<f32>(a.pingPongDir)
                                : 1.0f;
            a.time += dt * a.speed * dir;

            switch (a.loopMode)
            {
            case PropertyLoopMode::Once:
                if (a.time >= duration)
                {
                    a.time = duration;
                    a.playing = false;
                }
                else if (a.time < 0.0f)
                {
                    a.time = 0.0f;
                    a.playing = false;
                }
                break;
            case PropertyLoopMode::Loop:
                a.time -= duration * Floor(a.time / duration); // wraps negatives too
                break;
            case PropertyLoopMode::PingPong:
                // Fold into [0, duration], flipping direction at each end (bounded loop for big dt).
                for (int guard = 0; (a.time > duration || a.time < 0.0f) && guard < 64; ++guard)
                {
                    if (a.time > duration)
                    {
                        a.time = 2.0f * duration - a.time;
                        a.pingPongDir = -1;
                    }
                    else if (a.time < 0.0f)
                    {
                        a.time = -a.time;
                        a.pingPongDir = 1;
                    }
                }
                break;
            }
            return a.time;
        }

        scene::Scene* m_scene = nullptr;
    };
}
