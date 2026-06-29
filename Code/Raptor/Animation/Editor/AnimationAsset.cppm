/// Raptor::AnimationEditor — the `raptor.animation.editor` module.
///
/// Authoring/cook side: a SkeletonAsset / AnimationClipAsset wraps the cooked source + the source
/// file reference; the builders cook them into the content DB (Source -> product at load). Mirrors
/// raptor.geometry.editor. (The model importer — raptor.model IR -> these sources — lands later;
/// for now sources are populated round-trip from the runtime types via the resource layer.)

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module raptor.animation.editor;

import raptor.core;
import raptor.editor;
import raptor.content;
import raptor.animation;
import raptor.animation.resource;

using namespace raptor::core;

export namespace raptor::animation {

class SkeletonAsset final : public raptor::editor::Asset {
    RAPTOR_OBJECT(SkeletonAsset, raptor::editor::Asset)
public:
    SkeletonSource source;
    void Serialize(ISerializer& ar) override {
        raptor::editor::Asset::Serialize(ar);   // fileName (source model note)
        source.Serialize(ar);
    }
};

class AnimationClipAsset final : public raptor::editor::Asset {
    RAPTOR_OBJECT(AnimationClipAsset, raptor::editor::Asset)
public:
    AnimationClipSource source;
    void Serialize(ISerializer& ar) override {
        raptor::editor::Asset::Serialize(ar);
        source.Serialize(ar);
    }
};

// The animation graph is authored directly into its source (state machine, blend trees, clip refs).
class AnimationGraphAsset final : public raptor::editor::Asset {
    RAPTOR_OBJECT(AnimationGraphAsset, raptor::editor::Asset)
public:
    AnimationGraphSource source;
    void Serialize(ISerializer& ar) override {
        raptor::editor::Asset::Serialize(ar);
        source.Serialize(ar);
    }
};

class SkeletonAssetBuilder final : public raptor::editor::DefaultAssetBuilder {
public:
    [[nodiscard]] const TypeInfo* AssetType() const override { return &SkeletonAsset::StaticType(); }
    [[nodiscard]] Status Build(const raptor::editor::Asset& asset, raptor::editor::AssetBuildContext& ctx) override {
        const SkeletonAsset& a = static_cast<const SkeletonAsset&>(asset);
        return ctx.output->WriteObject(const_cast<SkeletonSource&>(a.source));
    }
};

class AnimationClipAssetBuilder final : public raptor::editor::DefaultAssetBuilder {
public:
    [[nodiscard]] const TypeInfo* AssetType() const override { return &AnimationClipAsset::StaticType(); }
    [[nodiscard]] Status Build(const raptor::editor::Asset& asset, raptor::editor::AssetBuildContext& ctx) override {
        const AnimationClipAsset& a = static_cast<const AnimationClipAsset&>(asset);
        return ctx.output->WriteObject(const_cast<AnimationClipSource&>(a.source));
    }
};

class AnimationGraphAssetBuilder final : public raptor::editor::DefaultAssetBuilder {
public:
    [[nodiscard]] const TypeInfo* AssetType() const override { return &AnimationGraphAsset::StaticType(); }
    [[nodiscard]] Status Build(const raptor::editor::Asset& asset, raptor::editor::AssetBuildContext& ctx) override {
        const AnimationGraphAsset& a = static_cast<const AnimationGraphAsset&>(asset);
        return ctx.output->WriteObject(const_cast<AnimationGraphSource&>(a.source));
    }
};

RAPTOR_DEFINE_OBJECT(SkeletonAsset, "raptor::animation")
RAPTOR_DEFINE_OBJECT(AnimationClipAsset, "raptor::animation")
RAPTOR_DEFINE_OBJECT(AnimationGraphAsset, "raptor::animation")

} // namespace raptor::animation
