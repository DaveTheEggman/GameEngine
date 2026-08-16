// Foundation::PropertyAnimation - module `foundation.propertyanimation`.
//
// The data model + evaluation + reflection binding resolver for animating ANY reflected property on
// ANY component with keyframe curves, as data (property-animation.md). Depends on foundation.core
// reflection ONLY - deliberately NOT part of foundation.animation (skeletal), so headless consumers
// (editor page, tests, MCP hosts) evaluate a property clip without pulling bones/poses/skinning.
//
// A clip is a set of tracks; each track targets one component-type + property PATH and holds either
// per-component scalar Curves (Float/Float3/Color) or quaternion keys (Quat, slerped - NEVER
// per-component euler). Evaluation yields a Variant per track at time t; the binding resolver walks
// the reflected property chain (nested paths included) and writes through reflection set, so
// RESOLVE-mode rules + any setter side effects apply exactly as if the inspector wrote the value.
// Scene-FREE: the resolver takes a reflection Instance; the engine binds entity -> component.

module;
#include "Core/Prelude.h"

export module foundation.propertyanimation;

import foundation.core;

using namespace foundation::core;

export namespace foundation::propertyanimation
{
    // The value a track animates. v1 set; Bool/enum STEP tracks are a later addition (the wire format
    // reserves the kind byte). Float/Float3/Color = per-component scalar curves; Quat = slerped keys.
    enum class TrackValueKind : u8
    {
        Float,
        Float3,
        Quat,
        Color,
    };

    // Scalar channels a kind uses (Float=1, Float3=3, Color=4). Quat=0 (it uses quatKeys, not curves).
    [[nodiscard]] inline u32 ChannelCount(TrackValueKind kind) noexcept
    {
        switch (kind)
        {
        case TrackValueKind::Float:
            return 1;
        case TrackValueKind::Float3:
            return 3;
        case TrackValueKind::Color:
            return 4;
        case TrackValueKind::Quat:
            return 0;
        }
        return 0;
    }

    // A quaternion keyframe (Quat tracks). Slerp between neighbours; shortest-arc handled by Slerp.
    struct QuatKey
    {
        f32 time = 0.0f;
        Quaternion value = Quaternion::Identity;
    };

    // Max channel count a scalar track needs (Color = 4).
    inline constexpr u32 kMaxChannels = 4;

    // One animated property. `componentType` is the reflected component's name (the engine resolves
    // it to a live component on the owning entity); `propertyPath` is dot-joined for nested props
    // (e.g. "color" or "light.color"). Channel storage: Float uses channels[0]; Float3 channels[0..2];
    // Color channels[0..3] (r,g,b,a); Quat uses quatKeys.
    struct PropertyTrack
    {
        String componentType;
        String propertyPath;
        TrackValueKind kind = TrackValueKind::Float;
        Curve channels[kMaxChannels];
        Array<QuatKey> quatKeys;

        // The track's own time span: the longest of its active channels / quat keys.
        [[nodiscard]] f32 Duration() const noexcept
        {
            f32 d = 0.0f;
            const u32 channelCount = ChannelCount(kind);
            for (u32 i = 0; i < channelCount; ++i)
            {
                d = Max(d, channels[i].Duration());
            }
            if (!quatKeys.IsEmpty())
            {
                d = Max(d, quatKeys[quatKeys.Size() - 1].time);
            }
            return d;
        }

        // Sample the track at `time` into a Variant of the matching type (Float->f32, Float3->Float3,
        // Color->Color, Quat->Quaternion). Callers wrap/clamp `time` (loop/pingpong is the clip's job).
        [[nodiscard]] Variant Sample(f32 time) const
        {
            switch (kind)
            {
            case TrackValueKind::Float:
                return Variant::From(channels[0].Evaluate(time));
            case TrackValueKind::Float3:
                return Variant::From(Float3{channels[0].Evaluate(time), channels[1].Evaluate(time),
                                            channels[2].Evaluate(time)});
            case TrackValueKind::Color:
                return Variant::From(Color{channels[0].Evaluate(time), channels[1].Evaluate(time),
                                           channels[2].Evaluate(time), channels[3].Evaluate(time)});
            case TrackValueKind::Quat:
                return Variant::From(SampleQuat(time));
            }
            return Variant{};
        }

        // Slerp the quaternion keys at `time` (clamped to the ends; shortest-arc via Slerp).
        [[nodiscard]] Quaternion SampleQuat(f32 time) const noexcept
        {
            const usize count = quatKeys.Size();
            if (count == 0)
            {
                return Quaternion::Identity;
            }
            if (count == 1 || time <= quatKeys[0].time)
            {
                return quatKeys[0].value;
            }
            if (time >= quatKeys[count - 1].time)
            {
                return quatKeys[count - 1].value;
            }
            usize i = 0;
            while (i + 1 < count && quatKeys[i + 1].time <= time)
            {
                ++i;
            }
            const QuatKey& a = quatKeys[i];
            const QuatKey& b = quatKeys[i + 1];
            const f32 segment = b.time - a.time;
            if (segment <= 1e-6f)
            {
                return b.value;
            }
            return Slerp(a.value, b.value, (time - a.time) / segment);
        }
    };

    // A cooked/authored clip: a duration, a loop hint, and the tracks. Immutable SHARED data at
    // runtime (each playing component owns only {time, state, binding cache}).
    struct PropertyAnimationClip
    {
        f32 duration = 0.0f;
        bool loop = false;
        Array<PropertyTrack> tracks;

        // The clip's duration = the longest track (call after editing to refresh `duration`).
        [[nodiscard]] f32 ComputeDuration() const noexcept
        {
            f32 d = 0.0f;
            for (const PropertyTrack& t : tracks)
            {
                d = Max(d, t.Duration());
            }
            return d;
        }
    };

    // ===================================================================================
    // Binding resolver (reflection only - no scene dependency)
    // ===================================================================================

    // A resolved property path: the chain of PropertyInfo from the component type down to the leaf.
    // PropertyInfo pointers are STABLE static type metadata, so caching the chain is safe; the nested
    // sub-instance ADDRESSES are re-walked on every write (the entity.get lesson: never cache raw
    // component/sub-object pointers across structural changes). Empty chain = unresolved.
    struct PropertyBinding
    {
        Array<const PropertyInfo*> chain;

        [[nodiscard]] bool IsResolved() const noexcept { return !chain.IsEmpty(); }
    };

    // Resolve a dot-joined `propertyPath` against `componentType`, walking Nested properties for the
    // intermediate segments. Returns an unresolved binding (empty chain) if any segment is missing or
    // an intermediate segment is not a Nested struct. Pure over static type metadata - no instance.
    [[nodiscard]] PropertyBinding ResolveBinding(const TypeInfo& componentType, StringView propertyPath);

    // Write `value` through a resolved binding into `componentInstance`. Re-derives each nested
    // sub-instance address from the live instance (never cached), then reflection-sets the leaf.
    // Returns InvalidArgument for an unresolved binding, NotFound if a nested address is null, or the
    // leaf setter's status. The value's type must match the leaf property (the caller's Sample does).
    [[nodiscard]] Status WriteBinding(const PropertyBinding& binding, const Instance& componentInstance,
                                      const Variant& value);
}
