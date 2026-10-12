// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Shaders.Pipeline - the custom shader's New Asset creator (File > New, asset_create): a starter
// .hlsl in Sources/ linked to a new ShaderAsset named like its instance. The starter is the
// shortest correct low-level shader: the engine's vertex stage, a properties block, and a pixel
// stage lit by the engine's lighting library that keeps the forward pass's contract (the DITHER
// fade, the ALPHA_TEST cut-out, the five GBUFFER targets), ready to be made one's own.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module shaders.pipeline;

import foundation.core;
import foundation.content;
import pipeline.core;

using namespace foundation::core;

namespace pipeline
{
    void RegisterShaderCreators(AssetCreatorRegistry& registry)
    {
        AssetCreator creator;
        creator.label = String(u8"Shader");
        creator.category = String(u8"Materials");
        creator.type = &ShaderAsset::StaticType();
        creator.defaultGroup = String(u8"Shaders");
        creator.run = [](const AssetCreationContext& context) -> foundation::content::Instance*
        {
            ShaderAsset asset;
            foundation::content::Instance* instance = CreateLinkedTextAsset(
                context, u8"Shader", u8".hlsl", kShaderStarter, ShaderAsset::StaticType(), asset);
            if (instance == nullptr)
            {
                return nullptr;
            }
            // Materials find a shader by its name: the instance's, unique in its group.
            asset.name = String(instance->Name());
            return instance->WriteObject(asset).IsOk() ? instance : nullptr;
        };
        registry.Register(Move(creator));
    }
}
