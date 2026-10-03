// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Pipeline::Render - the `render.pipeline` module (tooling): the render profile assets. An
// Environment Profile or a Post Process Profile carries the value fields of a scene's environment
// or post settings block (the block's own serializer writes them), cooks to its record by a copy,
// and is what a block whose source is Profile uses (engine.render :profiles).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module render.pipeline;

import foundation.core;
import foundation.content;
import pipeline.core;
import engine.render;

using namespace foundation::core;

export namespace pipeline
{
    class EnvironmentProfileAsset final : public pipeline::Asset
    {
        RTTI_OBJECT(EnvironmentProfileAsset, pipeline::Asset)
    public:
        engine::render::EnvironmentSettings values;

        void Serialize(ISerializer& ar) override
        {
            pipeline::Asset::Serialize(ar);
            engine::render::SerializeEnvironmentValues(ar, values, true);
        }
    };

    class PostProcessProfileAsset final : public pipeline::Asset
    {
        RTTI_OBJECT(PostProcessProfileAsset, pipeline::Asset)
    public:
        engine::render::PostProcessSettings values;

        void Serialize(ISerializer& ar) override
        {
            pipeline::Asset::Serialize(ar);
            engine::render::SerializePostValues(ar, values);
        }
    };

    class EnvironmentProfileAssetBuilder final : public pipeline::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &EnvironmentProfileAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &engine::render::EnvironmentProfileSource::StaticType();
        }
        // The sky texture is a runtime reference: its product must exist, but its edits never
        // re-cook the profile (the factory re-binds it at load).
        void ScanDependencies(const pipeline::Asset& asset, pipeline::AssetBuildContext&,
                              pipeline::AssetDependencies& out) override
        {
            const auto& profile = static_cast<const EnvironmentProfileAsset&>(asset);
            if (!profile.values.skyTexture.id.IsNil())
            {
                out.references.PushBack(profile.values.skyTexture.id);
            }
        }
        [[nodiscard]] Status Build(const pipeline::Asset& asset,
                                   pipeline::AssetBuildContext& ctx) override
        {
            if (ctx.output == nullptr)
            {
                return Status{ErrorCode::InvalidArgument};
            }
            engine::render::EnvironmentProfileSource cooked;
            cooked.values = static_cast<const EnvironmentProfileAsset&>(asset).values;
            return ctx.output->WriteObject(cooked);
        }
    };

    class PostProcessProfileAssetBuilder final : public pipeline::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &PostProcessProfileAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &engine::render::PostProcessProfileSource::StaticType();
        }
        void ScanDependencies(const pipeline::Asset& asset, pipeline::AssetBuildContext&,
                              pipeline::AssetDependencies& out) override
        {
            const auto& profile = static_cast<const PostProcessProfileAsset&>(asset);
            if (!profile.values.gradingLut.id.IsNil())
            {
                out.references.PushBack(profile.values.gradingLut.id);
            }
        }
        [[nodiscard]] Status Build(const pipeline::Asset& asset,
                                   pipeline::AssetBuildContext& ctx) override
        {
            if (ctx.output == nullptr)
            {
                return Status{ErrorCode::InvalidArgument};
            }
            engine::render::PostProcessProfileSource cooked;
            cooked.values = static_cast<const PostProcessProfileAsset&>(asset).values;
            return ctx.output->WriteObject(cooked);
        }
    };

    // Registers the asset types for content-DB construction + deserialization.
    inline void RegisterRenderProfileAssets()
    {
        GlobalTypeRegistry().Register(EnvironmentProfileAsset::StaticType(), TypeDomain(u8"Pipeline"));
        RegisterSerializable<EnvironmentProfileAsset>();
        GlobalTypeRegistry().Register(PostProcessProfileAsset::StaticType(), TypeDomain(u8"Pipeline"));
        RegisterSerializable<PostProcessProfileAsset>();
    }

    /// File > New's render creators (pipeline.registration composes every domain's).
    void RegisterRenderCreators(AssetCreatorRegistry& registry);

    RTTI_DEFINE_OBJECT(EnvironmentProfileAsset, "rtti::pipeline::render")
    RTTI_DEFINE_OBJECT(PostProcessProfileAsset, "rtti::pipeline::render")
}
