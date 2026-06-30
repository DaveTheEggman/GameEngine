/// Draconic::ModelImporter:anim_convert — Model IR skin/animation -> animation *Source.
///
/// Converts a model's skin (joints + inverse-bind matrices + the bone hierarchy) into a
/// SkeletonSource, and its animation channels into AnimationClipSources. The skeleton's
/// bone order IS the skin's JOINT order (so a skinned vertex's joint indices address the
/// skeleton directly), and parent links + animation target bones are remapped from model
/// bone indices into joint indices (NodeToBoneMapping).

module;
#include "Core/Prelude.h"

export module draconic.modelimporter:anim_convert;

import draconic.core;
import draconic.model;
import draconic.animation;
import draconic.animation.resource;

using namespace draconic::core;
namespace mdl = draconic::model;
namespace anim = draconic::animation;

export namespace draconic::modelimporter {

// model bone index -> skeleton joint index (the skin's joint order). -1 for bones not in the skin.
[[nodiscard]] inline HashMap<i32, i32> BuildBoneToJoint(const mdl::ModelSkin& skin)
{
    HashMap<i32, i32> map;
    const Span<const i32> joints = skin.joints();
    for (usize j = 0; j < joints.Size(); ++j) { map.InsertOrAssign(joints[j], static_cast<i32>(j)); }
    return map;
}

// Build a SkeletonSource from a skin: one bone per joint (joint order), local bind TRS from the
// model bone, inverse-bind from the skin, parent remapped into joint space.
inline void SkeletonSourceFromModel(const mdl::Model& model, const mdl::ModelSkin& skin,
                                    const HashMap<i32, i32>& boneToJoint, anim::SkeletonSource& out)
{
    out.name = String(u8"skeleton");
    const Span<const i32>  joints = skin.joints();
    const Span<const Mat4> ibms   = skin.inverseBindMatrices();
    const Span<mdl::ModelBone* const> bones = model.bones();

    for (usize j = 0; j < joints.Size(); ++j) {
        const i32 boneIdx = joints[j];
        const mdl::ModelBone* b = (boneIdx >= 0 && static_cast<usize>(boneIdx) < bones.Size()) ? bones[boneIdx] : nullptr;

        out.boneNames.PushBack(b != nullptr ? String(b->name()) : String{});
        i32 parentJoint = -1;
        if (b != nullptr && b->parentIndex >= 0) {
            const i32* p = boneToJoint.Find(b->parentIndex);
            if (p != nullptr) { parentJoint = *p; }
        }
        out.parentIndices.PushBack(parentJoint);
        out.translations.PushBack(b != nullptr ? b->translation : Vec3{ 0, 0, 0 });
        out.rotations.PushBack(b != nullptr ? b->rotation : Quat::Identity);
        out.scales.PushBack(b != nullptr ? b->scale : Vec3{ 1, 1, 1 });
        out.inverseBindPoses.PushBack(j < ibms.Size() ? ibms[j] : Mat4::Identity());
    }
}

// Build an AnimationClipSource from a model animation: each channel becomes a dense track keyed by
// JOINT index (channels targeting bones outside the skin, or morph-weight channels, are skipped).
inline void AnimationClipSourceFromModel(const mdl::ModelAnimation& animation, const HashMap<i32, i32>& boneToJoint,
                                         StringView name, anim::AnimationClipSource& out)
{
    out.name      = String(name);
    out.duration  = animation.duration;
    out.isLooping = true;

    for (const mdl::AnimationChannel* ch : animation.channels()) {
        if (ch == nullptr) { continue; }
        const i32* pj = boneToJoint.Find(ch->targetBone);
        if (pj == nullptr) { continue; }   // channel targets a bone not in this skin

        u8 kind = 0;
        switch (ch->path) {
        case mdl::AnimationPath::Translation: kind = static_cast<u8>(anim::AnimationClipSource::TrackKind::Position); break;
        case mdl::AnimationPath::Rotation:    kind = static_cast<u8>(anim::AnimationClipSource::TrackKind::Rotation); break;
        case mdl::AnimationPath::Scale:       kind = static_cast<u8>(anim::AnimationClipSource::TrackKind::Scale);    break;
        default: continue;   // Weights (morph) not supported
        }
        u8 interp = static_cast<u8>(anim::InterpolationMode::Linear);
        if (ch->interpolation == mdl::AnimationInterpolation::Step)        { interp = static_cast<u8>(anim::InterpolationMode::Step); }
        else if (ch->interpolation == mdl::AnimationInterpolation::CubicSpline) { interp = static_cast<u8>(anim::InterpolationMode::CubicSpline); }

        const Span<const mdl::AnimationKeyframe> keys = ch->keyframes();
        out.trackBone.PushBack(*pj);
        out.trackKind.PushBack(kind);
        out.trackInterp.PushBack(interp);
        out.trackStart.PushBack(static_cast<u32>(out.keyTimes.Size()));
        out.trackCount.PushBack(static_cast<u32>(keys.Size()));
        for (const mdl::AnimationKeyframe& k : keys) {
            out.keyTimes.PushBack(k.time);
            out.keyValues.PushBack(k.value);   // xyz for pos/scale, xyzw for rotation (matches FillClip)
        }
    }
}

} // namespace draconic::modelimporter
