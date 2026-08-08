/// Draconic::AnimationResource - the `draconic.animation.resource` module.
///
/// Skeletons + animation clips as resources: a cooked SkeletonSource / AnimationClipSource
/// (ISerializable - flat parallel arrays) is built by a factory into the runtime
/// animation::Skeleton / animation::AnimationClip. Mirrors draconic.geometry.resource
/// (Source -> Factory -> Product). The graph resource (composite, references clips) lands later.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.animation.resource;

import draconic.core;
import draconic.resource;
import draconic.content;
import draconic.animation;

using namespace draconic::core;
namespace resource = draconic::resource;

export namespace draconic::animation
{

    // ---- skeleton ------------------------------------------------------------------------------

    // Cooked skeleton: per-bone parallel arrays (name, parent, bind TRS, inverse bind matrix).
    class SkeletonSource : public ISerializable
    {
        DRACONIC_OBJECT(SkeletonSource, ISerializable)
    public:
        String name;
        Array<String> boneNames;
        Array<i32> parentIndices;
        Array<Float3> translations;
        Array<Quaternion> rotations;
        Array<Float3> scales;
        Array<Float4x4> inverseBindPoses;

        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "name", name);
            draconic::core::Serialize(ar, "boneNames", boneNames);
            draconic::core::Serialize(ar, "parentIndices", parentIndices);
            draconic::core::Serialize(ar, "translations", translations);
            draconic::core::Serialize(ar, "rotations", rotations);
            draconic::core::Serialize(ar, "scales", scales);
            draconic::core::Serialize(ar, "inverseBindPoses", inverseBindPoses);
        }

        // Capture a runtime Skeleton into this source (for cooking).
        static void FromSkeleton(const Skeleton& skel, SkeletonSource& out)
        {
            out.name = String(skel.Name().AsView());
            out.boneNames.Clear();
            out.parentIndices.Clear();
            out.translations.Clear();
            out.rotations.Clear();
            out.scales.Clear();
            out.inverseBindPoses.Clear();
            for (const Bone& b : skel.Bones())
            {
                out.boneNames.PushBack(String(b.name.AsView()));
                out.parentIndices.PushBack(b.parentIndex);
                out.translations.PushBack(b.localBindPose.position);
                out.rotations.PushBack(b.localBindPose.rotation);
                out.scales.PushBack(b.localBindPose.scale);
                out.inverseBindPoses.PushBack(b.inverseBindPose);
            }
        }

        // Populate a runtime Skeleton from this source (rebuilds name map + hierarchy).
        void FillSkeleton(Skeleton& skel) const
        {
            const i32 count = static_cast<i32>(parentIndices.Size());
            skel.ClearForReload(count);
            skel.Name() = String(name.AsView());
            Array<Bone>& bones = skel.Bones();
            for (usize i = 0; i < bones.Size(); ++i)
            {
                bones[i].index = static_cast<i32>(i);
                bones[i].parentIndex = parentIndices[i];
                if (i < boneNames.Size())
                {
                    bones[i].name = String(boneNames[i].AsView());
                }
                bones[i].localBindPose =
                    BoneTransform{i < translations.Size() ? translations[i] : Float3{0, 0, 0},
                                  i < rotations.Size() ? rotations[i] : Quaternion::Identity,
                                  i < scales.Size() ? scales[i] : Float3{1, 1, 1}};
                if (i < inverseBindPoses.Size())
                {
                    bones[i].inverseBindPose = inverseBindPoses[i];
                }
            }
            skel.BuildNameMap();
            skel.FindRootBones();
            skel.BuildChildIndices();
        }
    };

    class SkeletonFactory final : public resource::IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &Skeleton::StaticType();
        }
        [[nodiscard]] RefPtr<Object> Create(resource::ResourceManager& manager,
                                            draconic::content::Instance& instance) override
        {
            (void)manager;
            RefPtr<ISerializable> object = instance.ReadObject();
            SkeletonSource* src = Cast<SkeletonSource>(object.Get());
            if (src == nullptr)
            {
                return RefPtr<Object>{};
            }
            RefPtr<Skeleton> skel = MakeRef<Skeleton>(DefaultAllocator());
            src->FillSkeleton(*skel);
            return skel;
        }
    };

    // ---- animation clip ------------------------------------------------------------------------

    // Cooked clip: per-track metadata + a dense keyframe pool (times + Float4 values: xyz for
    // position/scale, xyzw for rotation), plus events.
    class AnimationClipSource : public ISerializable
    {
        DRACONIC_OBJECT(AnimationClipSource, ISerializable)
    public:
        enum class TrackKind : u8
        {
            Position = 0,
            Rotation = 1,
            Scale = 2
        };

        String name;
        f32 duration = 0.0f;
        bool isLooping = false;
        Array<i32> trackBone;  // bone index per track
        Array<u8> trackKind;   // TrackKind
        Array<u8> trackInterp; // InterpolationMode
        Array<u32> trackStart; // first keyframe index into keyTimes/keyValues
        Array<u32> trackCount; // keyframe count
        Array<f32> keyTimes;
        Array<Float4> keyValues; // position/scale in xyz; rotation in xyzw
        Array<f32> eventTimes;
        Array<String> eventNames;

        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "name", name);
            draconic::core::Serialize(ar, "duration", duration);
            draconic::core::Serialize(ar, "isLooping", isLooping);
            draconic::core::Serialize(ar, "trackBone", trackBone);
            draconic::core::Serialize(ar, "trackKind", trackKind);
            draconic::core::Serialize(ar, "trackInterp", trackInterp);
            draconic::core::Serialize(ar, "trackStart", trackStart);
            draconic::core::Serialize(ar, "trackCount", trackCount);
            draconic::core::Serialize(ar, "keyTimes", keyTimes);
            draconic::core::Serialize(ar, "keyValues", keyValues);
            draconic::core::Serialize(ar, "eventTimes", eventTimes);
            draconic::core::Serialize(ar, "eventNames", eventNames);
        }

        static void FromClip(const AnimationClip& clip, AnimationClipSource& out)
        {
            out.name = String(clip.Name().AsView());
            out.duration = clip.duration;
            out.isLooping = clip.isLooping;
            out.trackBone.Clear();
            out.trackKind.Clear();
            out.trackInterp.Clear();
            out.trackStart.Clear();
            out.trackCount.Clear();
            out.keyTimes.Clear();
            out.keyValues.Clear();
            for (const auto& t : clip.PositionTracks())
            {
                AppendVec3Track(out, *t, TrackKind::Position);
            }
            for (const auto& t : clip.RotationTracks())
            {
                AppendQuatTrack(out, *t);
            }
            for (const auto& t : clip.ScaleTracks())
            {
                AppendVec3Track(out, *t, TrackKind::Scale);
            }
            out.eventTimes.Clear();
            out.eventNames.Clear();
            for (const AnimationEvent& e : clip.Events())
            {
                out.eventTimes.PushBack(e.time);
                out.eventNames.PushBack(String(e.name.AsView()));
            }
        }

        void FillClip(AnimationClip& clip) const
        {
            clip.ClearForReload();
            clip.Name() = String(name.AsView());
            clip.duration = duration;
            clip.isLooping = isLooping;
            for (usize i = 0; i < trackBone.Size(); ++i)
            {
                const TrackKind kind =
                    static_cast<TrackKind>(i < trackKind.Size() ? trackKind[i] : 0);
                const InterpolationMode interp =
                    static_cast<InterpolationMode>(i < trackInterp.Size() ? trackInterp[i] : 1);
                const u32 start = trackStart[i];
                const u32 count = trackCount[i];
                if (kind == TrackKind::Rotation)
                {
                    AnimationTrack<Quaternion>* track = clip.GetOrCreateRotationTrack(trackBone[i]);
                    track->interpolation = interp;
                    for (u32 j = 0; j < count; ++j)
                    {
                        const Float4 v = keyValues[start + j];
                        track->AddKeyframe(keyTimes[start + j], Quaternion{v.x, v.y, v.z, v.w});
                    }
                }
                else
                {
                    AnimationTrack<Float3>* track =
                        (kind == TrackKind::Scale) ? clip.GetOrCreateScaleTrack(trackBone[i])
                                                   : clip.GetOrCreatePositionTrack(trackBone[i]);
                    track->interpolation = interp;
                    for (u32 j = 0; j < count; ++j)
                    {
                        const Float4 v = keyValues[start + j];
                        track->AddKeyframe(keyTimes[start + j], Float3{v.x, v.y, v.z});
                    }
                }
            }
            for (usize i = 0; i < eventTimes.Size(); ++i)
            {
                clip.AddEvent(eventTimes[i],
                              (i < eventNames.Size()) ? eventNames[i].AsView() : StringView{});
            }
        }

    private:
        static void AppendVec3Track(AnimationClipSource& out, const AnimationTrack<Float3>& track,
                                    TrackKind kind)
        {
            out.trackBone.PushBack(track.boneIndex);
            out.trackKind.PushBack(static_cast<u8>(kind));
            out.trackInterp.PushBack(static_cast<u8>(track.interpolation));
            out.trackStart.PushBack(static_cast<u32>(out.keyTimes.Size()));
            out.trackCount.PushBack(static_cast<u32>(track.Keyframes().Size()));
            for (const Keyframe<Float3>& k : track.Keyframes())
            {
                out.keyTimes.PushBack(k.time);
                out.keyValues.PushBack(Float4{k.value.x, k.value.y, k.value.z, 0.0f});
            }
        }
        static void AppendQuatTrack(AnimationClipSource& out,
                                    const AnimationTrack<Quaternion>& track)
        {
            out.trackBone.PushBack(track.boneIndex);
            out.trackKind.PushBack(static_cast<u8>(TrackKind::Rotation));
            out.trackInterp.PushBack(static_cast<u8>(track.interpolation));
            out.trackStart.PushBack(static_cast<u32>(out.keyTimes.Size()));
            out.trackCount.PushBack(static_cast<u32>(track.Keyframes().Size()));
            for (const Keyframe<Quaternion>& k : track.Keyframes())
            {
                out.keyTimes.PushBack(k.time);
                out.keyValues.PushBack(Float4{k.value.x, k.value.y, k.value.z, k.value.w});
            }
        }
    };

    class AnimationClipFactory final : public resource::IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &AnimationClip::StaticType();
        }
        [[nodiscard]] RefPtr<Object> Create(resource::ResourceManager& manager,
                                            draconic::content::Instance& instance) override
        {
            (void)manager;
            RefPtr<ISerializable> object = instance.ReadObject();
            AnimationClipSource* src = Cast<AnimationClipSource>(object.Get());
            if (src == nullptr)
            {
                return RefPtr<Object>{};
            }
            RefPtr<AnimationClip> clip = MakeRef<AnimationClip>(DefaultAllocator());
            src->FillClip(*clip);
            return clip;
        }
    };

    // ---- animation graph (authored, composite) -------------------------------------------------
    // The graph is authored directly (its source IS the authoritative form, unlike skeleton/clip which
    // are captured from models). The factory builds a runtime AnimationGraph, resolving each clip
    // reference (Guid) via manager.Bind<AnimationClip> - which records a dependency edge, so the manager
    // keeps the clips alive while the graph is bound (the graph's nodes hold borrowed AnimationClip*).

    // Authored sub-records - plain copyable value structs (so they live in Array<T>), each with a free
    // Serialize overload found by ADL from the Array<T> serializer. (Not ISerializable, which is
    // non-copyable.) One state node: a clip, or a 1D/2D blend tree; clips referenced by resource id.
    struct GraphNodeData
    {
        u8 kind = 0;                            // 0 = clip, 1 = blend1d, 2 = blend2d
        Guid clipRef;                           // clip node
        i32 paramIndex = -1;                    // blend1d driver
        i32 paramIndexX = -1, paramIndexY = -1; // blend2d drivers
        Array<f32> entryThresholds;             // blend1d, parallel to entryClips
        Array<Float2> entryPositions;           // blend2d, parallel to entryClips
        Array<Guid> entryClips;
    };
    inline void Serialize(ISerializer& ar, GraphNodeData& n)
    {
        draconic::core::Serialize(ar, "kind", n.kind);
        draconic::core::Serialize(ar, "clipRef", n.clipRef);
        draconic::core::Serialize(ar, "paramIndex", n.paramIndex);
        draconic::core::Serialize(ar, "paramIndexX", n.paramIndexX);
        draconic::core::Serialize(ar, "paramIndexY", n.paramIndexY);
        draconic::core::Serialize(ar, "entryThresholds", n.entryThresholds);
        draconic::core::Serialize(ar, "entryPositions", n.entryPositions);
        draconic::core::Serialize(ar, "entryClips", n.entryClips);
    }

    struct GraphConditionData
    {
        i32 paramIndex = 0;
        u8 op = 0;
        f32 threshold = 0.0f;
    };
    inline void Serialize(ISerializer& ar, GraphConditionData& c)
    {
        draconic::core::Serialize(ar, "paramIndex", c.paramIndex);
        draconic::core::Serialize(ar, "op", c.op);
        draconic::core::Serialize(ar, "threshold", c.threshold);
    }

    struct GraphTransitionData
    {
        i32 src = -1, dst = 0;
        f32 duration = 0.25f;
        bool hasExitTime = false;
        f32 exitTime = 1.0f;
        i32 priority = 0;
        Array<GraphConditionData> conditions;
    };
    inline void Serialize(ISerializer& ar, GraphTransitionData& t)
    {
        draconic::core::Serialize(ar, "src", t.src);
        draconic::core::Serialize(ar, "dst", t.dst);
        draconic::core::Serialize(ar, "duration", t.duration);
        draconic::core::Serialize(ar, "hasExitTime", t.hasExitTime);
        draconic::core::Serialize(ar, "exitTime", t.exitTime);
        draconic::core::Serialize(ar, "priority", t.priority);
        draconic::core::Serialize(ar, "conditions", t.conditions);
    }

    struct GraphStateData
    {
        String name;
        f32 speed = 1.0f;
        bool loop = true;
        GraphNodeData node;
    };
    inline void Serialize(ISerializer& ar, GraphStateData& s)
    {
        draconic::core::Serialize(ar, "name", s.name);
        draconic::core::Serialize(ar, "speed", s.speed);
        draconic::core::Serialize(ar, "loop", s.loop);
        draconic::core::Serialize(ar, "node", s.node);
    }

    struct GraphLayerData
    {
        String name;
        i32 defaultState = 0;
        u8 blendMode = 0;
        f32 weight = 1.0f;
        Array<f32> maskWeights; // empty = no mask
        Array<GraphStateData> states;
        Array<GraphTransitionData> transitions;
    };
    inline void Serialize(ISerializer& ar, GraphLayerData& l)
    {
        draconic::core::Serialize(ar, "name", l.name);
        draconic::core::Serialize(ar, "defaultState", l.defaultState);
        draconic::core::Serialize(ar, "blendMode", l.blendMode);
        draconic::core::Serialize(ar, "weight", l.weight);
        draconic::core::Serialize(ar, "maskWeights", l.maskWeights);
        draconic::core::Serialize(ar, "states", l.states);
        draconic::core::Serialize(ar, "transitions", l.transitions);
    }

    // Cooked graph: parameters (name/type + default values) + layers (states/transitions/mask).
    class AnimationGraphSource : public ISerializable
    {
        DRACONIC_OBJECT(AnimationGraphSource, ISerializable)
    public:
        Array<String> paramNames;
        Array<u8> paramTypes;   // AnimationParameterType
        Array<f32> paramFloats; // default values (per parameter, parallel)
        Array<i32> paramInts;
        Array<u8> paramBools;
        Array<GraphLayerData> layers;

        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "paramNames", paramNames);
            draconic::core::Serialize(ar, "paramTypes", paramTypes);
            draconic::core::Serialize(ar, "paramFloats", paramFloats);
            draconic::core::Serialize(ar, "paramInts", paramInts);
            draconic::core::Serialize(ar, "paramBools", paramBools);
            draconic::core::Serialize(ar, "layers", layers);
        }

        // Build a runtime AnimationGraph, resolving clip references through the manager.
        void BuildInto(resource::ResourceManager& manager, AnimationGraph& graph) const
        {
            for (usize i = 0; i < paramNames.Size(); ++i)
            {
                const auto type =
                    static_cast<AnimationParameterType>(i < paramTypes.Size() ? paramTypes[i] : 0);
                const i32 idx = graph.AddParameter(paramNames[i].AsView(), type);
                if (AnimationGraphParameter* p = graph.GetParameter(idx))
                {
                    if (i < paramFloats.Size())
                    {
                        p->floatValue = paramFloats[i];
                    }
                    if (i < paramInts.Size())
                    {
                        p->intValue = paramInts[i];
                    }
                    if (i < paramBools.Size())
                    {
                        p->boolValue = paramBools[i] != 0;
                    }
                }
            }
            for (const GraphLayerData& ld : layers)
            {
                UniquePtr<AnimationLayer> layer =
                    MakeUnique<AnimationLayer>(DefaultAllocator(), ld.name.AsView());
                layer->defaultStateIndex = ld.defaultState;
                layer->blendMode = static_cast<LayerBlendMode>(ld.blendMode);
                layer->weight = ld.weight;
                if (!ld.maskWeights.IsEmpty())
                {
                    UniquePtr<BoneMask> mask = MakeUnique<BoneMask>(
                        DefaultAllocator(), static_cast<i32>(ld.maskWeights.Size()), 0.0f);
                    for (usize b = 0; b < ld.maskWeights.Size(); ++b)
                    {
                        mask->SetWeight(static_cast<i32>(b), ld.maskWeights[b]);
                    }
                    layer->SetMask(static_cast<UniquePtr<BoneMask>&&>(mask));
                }
                for (const GraphStateData& sd : ld.states)
                {
                    UniquePtr<AnimationGraphState> state = MakeUnique<AnimationGraphState>(
                        DefaultAllocator(), sd.name.AsView(), BuildNode(manager, sd.node));
                    state->speed = sd.speed;
                    state->loop = sd.loop;
                    layer->AddState(static_cast<UniquePtr<AnimationGraphState>&&>(state));
                }
                for (const GraphTransitionData& td : ld.transitions)
                {
                    UniquePtr<AnimationGraphTransition> tr =
                        MakeUnique<AnimationGraphTransition>(DefaultAllocator());
                    tr->sourceStateIndex = td.src;
                    tr->destStateIndex = td.dst;
                    tr->duration = td.duration;
                    tr->hasExitTime = td.hasExitTime;
                    tr->exitTime = td.exitTime;
                    tr->priority = td.priority;
                    for (const GraphConditionData& cd : td.conditions)
                    {
                        tr->Conditions().PushBack(AnimationGraphCondition{
                            cd.paramIndex, static_cast<ComparisonOp>(cd.op), cd.threshold});
                    }
                    layer->AddTransition(static_cast<UniquePtr<AnimationGraphTransition>&&>(tr));
                }
                graph.AddLayer(static_cast<UniquePtr<AnimationLayer>&&>(layer));
            }
        }

    private:
        // Resolve a clip reference (null id -> null clip). Bind records the graph->clip dependency edge.
        [[nodiscard]] static AnimationClip* ResolveClip(resource::ResourceManager& manager,
                                                        const Guid& id)
        {
            return id.IsNil() ? nullptr : manager.Bind<AnimationClip>(id).Get();
        }
        [[nodiscard]] static UniquePtr<IAnimationStateNode>
        BuildNode(resource::ResourceManager& manager, const GraphNodeData& n)
        {
            if (n.kind == 1)
            { // blend1d
                UniquePtr<BlendTree1D> t = MakeUnique<BlendTree1D>(DefaultAllocator());
                t->parameterIndex = n.paramIndex;
                for (usize i = 0; i < n.entryClips.Size(); ++i)
                {
                    t->AddEntry(i < n.entryThresholds.Size() ? n.entryThresholds[i] : 0.0f,
                                ResolveClip(manager, n.entryClips[i]));
                }
                return t;
            }
            if (n.kind == 2)
            { // blend2d
                UniquePtr<BlendTree2D> t = MakeUnique<BlendTree2D>(DefaultAllocator());
                t->parameterIndexX = n.paramIndexX;
                t->parameterIndexY = n.paramIndexY;
                for (usize i = 0; i < n.entryClips.Size(); ++i)
                {
                    t->AddEntry(i < n.entryPositions.Size() ? n.entryPositions[i] : Float2{0, 0},
                                ResolveClip(manager, n.entryClips[i]));
                }
                return t;
            }
            return MakeUnique<ClipStateNode>(DefaultAllocator(),
                                             ResolveClip(manager, n.clipRef)); // clip node
        }
    };

    class AnimationGraphFactory final : public resource::IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &AnimationGraph::StaticType();
        }
        [[nodiscard]] RefPtr<Object> Create(resource::ResourceManager& manager,
                                            draconic::content::Instance& instance) override
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            AnimationGraphSource* src = Cast<AnimationGraphSource>(object.Get());
            if (src == nullptr)
            {
                return RefPtr<Object>{};
            }
            RefPtr<AnimationGraph> graph = MakeRef<AnimationGraph>(DefaultAllocator());
            src->BuildInto(manager, *graph);
            return graph;
        }
    };

    DRACONIC_DEFINE_OBJECT(SkeletonSource, "rtti::animation")
    DRACONIC_DEFINE_OBJECT(AnimationClipSource, "rtti::animation")
    DRACONIC_DEFINE_OBJECT(AnimationGraphSource, "rtti::animation")

} // namespace draconic::animation
