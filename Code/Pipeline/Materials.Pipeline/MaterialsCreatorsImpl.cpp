// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Materials.Pipeline - the materials domain's New Asset creators
// (agent-playtesting-and-asset-creation.md P1): what File > New and asset_create make, with no
// editor (the source database and the sources folder are all a creation needs).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module materials.pipeline;

import foundation.core;
import foundation.content;
import foundation.materials;
import pipeline.core;

using namespace foundation::core;

namespace pipeline
{
    foundation::content::Instance* CreateMaterialInstance(foundation::content::Group* target,
                                                          StringView baseName, bool unlit)
    {
        if (target == nullptr)
        {
            return nullptr;
        }
        const String name = target->UniqueInstanceName(baseName);
        foundation::content::Instance* instance =
            target->CreateInstance(name.AsView(), MaterialAsset::StaticType());
        if (instance == nullptr)
        {
            return nullptr;
        }
        RefPtr<foundation::materials::Material> built =
            unlit ? foundation::materials::CreateUnlit(name.AsView())
                  : foundation::materials::CreatePBR(name.AsView());
        MaterialAsset asset;
        MaterialImporter::Import(*built, Guid{}, asset);
        return instance->WriteObject(asset).IsOk() ? instance : nullptr;
    }

    void RegisterMaterialCreators(AssetCreatorRegistry& registry)
    {
        // A preset material, PBR or unlit, under Materials/ unless a group was picked.
        const auto add = [&](StringView label, bool unlit)
        {
            AssetCreator creator;
            creator.label = String(label);
            creator.category = String(u8"Materials");
            creator.type = &MaterialAsset::StaticType();
            creator.defaultGroup = String(u8"Materials");
            creator.run = [unlit](const AssetCreationContext& context)
            { return CreateMaterialInstance(context.Target(), context.NameOr(u8"Material"), unlit); };
            registry.Register(Move(creator));
        };
        add(u8"PBR Material", false);
        add(u8"Unlit Material", true);
    }
}
