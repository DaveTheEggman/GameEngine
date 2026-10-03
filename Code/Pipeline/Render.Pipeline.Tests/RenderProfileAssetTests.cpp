// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The render profile assets: a profile asset round-trips its values through its source envelope,
// cooks to its record by a copy, loads through the runtime factory, and declares its texture
// reference so the cook knows the profile needs it.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.render;
import pipeline.core;
import engine.render;
import render.pipeline;

using namespace foundation::core;
using namespace pipeline;
namespace content = foundation::content;
namespace render = engine::render;

namespace
{
    void RemoveTree(StringView root)
    {
        foundation::vfs::NativeFileSystem fs(root, DefaultAllocator());
        Array<foundation::vfs::DirEntry> entries;
        if (fs.AsEnumerable()->Enumerate(u8"", entries).IsOk())
        {
            for (const auto& e : entries)
            {
                if (!e.isDirectory)
                {
                    (void)fs.AsWritable()->Delete(e.name.AsView());
                }
            }
        }
        (void)RemoveDirectory(root);
    }
}

TEST_CASE("render.pipeline: an Environment Profile cooks and loads with its values")
{
    RegisterRenderProfileAssets();
    render::RegisterRenderProfileResources();
    const StringView dir = u8"scratch_render_pipeline_db";
    RemoveTree(dir);
    {
        foundation::vfs::NativeFileSystem mount(dir, DefaultAllocator());
        content::ContentDatabase db(DefaultAllocator(), mount, BinarySerializerFactory(), u8".rasset");

        // The source asset round-trips its values (the block's own serializer).
        EnvironmentProfileAsset asset;
        asset.values.ambientIntensity = 0.08f;
        asset.values.skyMode = foundation::render::SkyMode::Analytic;
        asset.values.shadowDistance = 60.0f;
        asset.values.skyTexture.id = Guid{0x5u, 0x6u};
        auto* source = db.RootGroup()->CreateInstance(u8"dusk", EnvironmentProfileAsset::StaticType());
        REQUIRE(source->WriteObject(asset).IsOk());
        RefPtr<ISerializable> back = source->ReadObject();
        auto* read = Cast<EnvironmentProfileAsset>(back.Get());
        REQUIRE(read != nullptr);
        CHECK(read->values.shadowDistance == doctest::Approx(60.0f));
        CHECK(read->values.skyTexture.id == Guid{0x5u, 0x6u});

        // The cook copies the values; the sky texture is a reference the product needs.
        EnvironmentProfileAssetBuilder builder;
        AssetDependencies dependencies;
        AssetBuildContext ctx{DefaultAllocator()};
        builder.ScanDependencies(asset, ctx, dependencies);
        REQUIRE(dependencies.references.Size() == 1u);
        CHECK(dependencies.references[0] == Guid{0x5u, 0x6u});
        asset.values.skyTexture.id = Guid{}; // no texture product in this database
        auto* cooked =
            db.RootGroup()->CreateInstance(u8"dusk.cooked", render::EnvironmentProfileSource::StaticType());
        ctx.output = cooked;
        REQUIRE(builder.Build(asset, ctx).IsOk());

        render::EnvironmentProfileFactory factory(DefaultAllocator());
        foundation::resource::ResourceManager manager(DefaultAllocator(), db);
        manager.AddFactory(&factory);
        foundation::resource::Proxy<render::EnvironmentProfile> profile =
            manager.Bind<render::EnvironmentProfile>(cooked->Id());
        REQUIRE(profile);
        CHECK(profile->values.ambientIntensity == doctest::Approx(0.08f));
        CHECK(profile->values.skyMode == foundation::render::SkyMode::Analytic);
        CHECK(profile->values.shadowDistance == doctest::Approx(60.0f));
    }
    RemoveTree(dir);
}

TEST_CASE("render.pipeline: a Post Process Profile cooks and loads with its values")
{
    RegisterRenderProfileAssets();
    render::RegisterRenderProfileResources();
    const StringView dir = u8"scratch_render_pipeline_post_db";
    RemoveTree(dir);
    {
        foundation::vfs::NativeFileSystem mount(dir, DefaultAllocator());
        content::ContentDatabase db(DefaultAllocator(), mount, BinarySerializerFactory(), u8".rasset");
        PostProcessProfileAsset asset;
        asset.values.exposureEV = 1.0f;
        asset.values.aaMode = render::AaMode::FXAA;
        asset.values.aoMode = foundation::render::AoMode::GTAO;
        auto* cooked =
            db.RootGroup()->CreateInstance(u8"look", render::PostProcessProfileSource::StaticType());
        PostProcessProfileAssetBuilder builder;
        AssetBuildContext ctx{DefaultAllocator()};
        ctx.output = cooked;
        REQUIRE(builder.Build(asset, ctx).IsOk());

        render::PostProcessProfileFactory factory(DefaultAllocator());
        foundation::resource::ResourceManager manager(DefaultAllocator(), db);
        manager.AddFactory(&factory);
        foundation::resource::Proxy<render::PostProcessProfile> profile =
            manager.Bind<render::PostProcessProfile>(cooked->Id());
        REQUIRE(profile);
        CHECK(profile->values.exposureEV == doctest::Approx(1.0f));
        CHECK(profile->values.aaMode == render::AaMode::FXAA);
        CHECK(profile->values.aoMode == foundation::render::AoMode::GTAO);
    }
    RemoveTree(dir);
}
