// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Pipeline::Animation - the `foundation.animation.editor` module.
///
/// Authoring/cook side: a SkeletonAsset / AnimationClipAsset wraps the cooked source + the source
/// file reference; the builders cook them into the content DB (Source -> product at load). Mirrors
/// foundation.geometry.editor. (Sources are populated round-trip from the runtime types via the
/// resource layer; the model importer - foundation.model IR -> these sources - is a later transform.)

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module animation.pipeline;

import foundation.core;
import pipeline.core;
import foundation.content;
import foundation.animation;
import foundation.animation.resource;

using namespace foundation::core;
using namespace foundation::animation;

export namespace pipeline{

    class SkeletonAsset final : public pipeline::Asset
    {
        RTTI_OBJECT(SkeletonAsset, pipeline::Asset)
    public:
        SkeletonSource source;
        void Serialize(ISerializer& ar) override
        {
            pipeline::Asset::Serialize(ar); // fileName (source model note)
            source.Serialize(ar);
        }
    };

    class AnimationClipAsset final : public pipeline::Asset
    {
        RTTI_OBJECT(AnimationClipAsset, pipeline::Asset)
    public:
        AnimationClipSource source;
        void Serialize(ISerializer& ar) override
        {
            pipeline::Asset::Serialize(ar);
            source.Serialize(ar);
        }
    };

    // The animation graph is authored directly into its source (state machine, blend trees, clip refs).
    // Editor-only canvas layout rides on the ASSET (the builder cooks `source` alone, so none of it
    // reaches the runtime wire): per layer, per state, the node position on the graph canvas -
    // parallel to source.layers[i].states (the page keeps them in sync on add/remove).
    class AnimationGraphAsset final : public pipeline::Asset
    {
        RTTI_OBJECT(AnimationGraphAsset, pipeline::Asset)
    public:
        AnimationGraphSource source;
        Array<Array<Float2>> layerStatePositions;
        Array<Float2> layerAnyStatePositions; // the per-layer "Any State" pseudo-node
        void Serialize(ISerializer& ar) override
        {
            pipeline::Asset::Serialize(ar);
            source.Serialize(ar);
            foundation::core::Serialize(ar, "layerStatePositions", layerStatePositions);
            foundation::core::Serialize(ar, "layerAnyStatePositions", layerAnyStatePositions);
        }
    };

    class SkeletonAssetBuilder final : public pipeline::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &SkeletonAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &SkeletonSource::StaticType();
        }
        [[nodiscard]] Status Build(const pipeline::Asset& asset,
                                   pipeline::AssetBuildContext& ctx) override
        {
            const SkeletonAsset& a = static_cast<const SkeletonAsset&>(asset);
            return ctx.output->WriteObject(const_cast<SkeletonSource&>(a.source));
        }
    };

    class AnimationClipAssetBuilder final : public pipeline::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &AnimationClipAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &AnimationClipSource::StaticType();
        }
        [[nodiscard]] Status Build(const pipeline::Asset& asset,
                                   pipeline::AssetBuildContext& ctx) override
        {
            const AnimationClipAsset& a = static_cast<const AnimationClipAsset&>(asset);
            return ctx.output->WriteObject(const_cast<AnimationClipSource&>(a.source));
        }
    };

    // Registers the animation asset types for content-DB construction + deserialization.
    inline void RegisterAnimationAssets()
    {
        GlobalTypeRegistry().Register(SkeletonAsset::StaticType(), TypeDomain(u8"Pipeline"));
        GlobalTypeRegistry().Register(AnimationClipAsset::StaticType(), TypeDomain(u8"Pipeline"));
        GlobalTypeRegistry().Register(AnimationGraphAsset::StaticType(), TypeDomain(u8"Pipeline"));
        RegisterSerializable<SkeletonAsset>();
        RegisterSerializable<AnimationClipAsset>();
        RegisterSerializable<AnimationGraphAsset>();
        // The graph's cooked PRODUCT: ReadObject constructs it by type name in cook hosts, so
        // it must be registered here too (the clip/skeleton products ride
        // RegisterModelResourceTypes; the graph source has no other registration site).
        GlobalTypeRegistry().Register(foundation::animation::AnimationGraphSource::StaticType());
        RegisterSerializable<foundation::animation::AnimationGraphSource>();
    }

    class AnimationGraphAssetBuilder final : public pipeline::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &AnimationGraphAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &AnimationGraphSource::StaticType();
        }
        [[nodiscard]] Status Build(const pipeline::Asset& asset,
                                   pipeline::AssetBuildContext& ctx) override
        {
            const AnimationGraphAsset& a = static_cast<const AnimationGraphAsset&>(asset);
            return ctx.output->WriteObject(const_cast<AnimationGraphSource&>(a.source));
        }
    };

    RTTI_DEFINE_OBJECT(SkeletonAsset, "rtti::pipeline::animation")
    RTTI_DEFINE_OBJECT(AnimationClipAsset, "rtti::pipeline::animation")
    RTTI_DEFINE_OBJECT(AnimationGraphAsset, "rtti::pipeline::animation")

} // namespace foundation::animation
