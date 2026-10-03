// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Render.Pipeline - the render domain's New Asset creators: what File > New and asset_create make
// (the render profiles, at the engine's defaults).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module render.pipeline;

import foundation.core;
import foundation.content;
import pipeline.core;

using namespace foundation::core;

namespace pipeline
{
    void RegisterRenderCreators(AssetCreatorRegistry& registry)
    {
        {
            AssetCreator creator;
            creator.label = String(u8"Environment Profile");
            creator.category = String(u8"Rendering");
            creator.defaultGroup = u8"Profiles";
            creator.type = &EnvironmentProfileAsset::StaticType();
            creator.run = [](const AssetCreationContext& context) -> foundation::content::Instance*
            {
                EnvironmentProfileAsset asset;
                return CreateWrittenInstance(context.Target(), context.NameOr(u8"EnvironmentProfile"),
                                             EnvironmentProfileAsset::StaticType(), asset);
            };
            registry.Register(Move(creator));
        }
        {
            AssetCreator creator;
            creator.label = String(u8"Post Process Profile");
            creator.category = String(u8"Rendering");
            creator.defaultGroup = u8"Profiles";
            creator.type = &PostProcessProfileAsset::StaticType();
            creator.run = [](const AssetCreationContext& context) -> foundation::content::Instance*
            {
                PostProcessProfileAsset asset;
                return CreateWrittenInstance(context.Target(), context.NameOr(u8"PostProcessProfile"),
                                             PostProcessProfileAsset::StaticType(), asset);
            };
            registry.Register(Move(creator));
        }
    }
}
