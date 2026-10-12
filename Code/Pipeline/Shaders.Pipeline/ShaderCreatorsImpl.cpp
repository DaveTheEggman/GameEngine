// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Shaders.Pipeline - the custom shaders' New Asset creators (File > New, asset_create): a starter
// .hlsl in Sources/ linked to a new ShaderAsset named like its instance. Lit and Unlit Surface
// Shader start from a Surface() the engine wraps (the lava, a coloured texture); Low-Level Shader
// from the shortest correct low-level shader: the engine's vertex stage, a properties block, and a
// pixel stage lit by the engine's lighting library that keeps the forward pass's contract.

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
        const auto add = [&](StringView label, StringView baseName, StringView starter)
        {
            AssetCreator creator;
            creator.label = String(label);
            creator.category = String(u8"Materials");
            creator.type = &ShaderAsset::StaticType();
            creator.defaultGroup = String(u8"Shaders");
            creator.run = [baseName, starter](const AssetCreationContext& context)
                -> foundation::content::Instance*
            {
                ShaderAsset asset;
                foundation::content::Instance* instance = CreateLinkedTextAsset(
                    context, baseName, u8".hlsl", starter, ShaderAsset::StaticType(), asset);
                if (instance == nullptr)
                {
                    return nullptr;
                }
                // Materials find a shader by its name: the instance's, unique in its group.
                asset.name = String(instance->Name());
                return instance->WriteObject(asset).IsOk() ? instance : nullptr;
            };
            registry.Register(Move(creator));
        };
        add(u8"Lit Surface Shader", u8"SurfaceShader", kLitSurfaceShaderStarter);
        add(u8"Unlit Surface Shader", u8"UnlitShader", kUnlitSurfaceShaderStarter);
        add(u8"Low-Level Shader", u8"Shader", kShaderStarter);
    }
}
