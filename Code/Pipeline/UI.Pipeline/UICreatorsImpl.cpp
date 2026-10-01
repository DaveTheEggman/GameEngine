// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI.Pipeline - the UI domain's New Asset creators
// (agent-playtesting-and-asset-creation.md P1): what File > New and asset_create make, with no
// editor (the source database and the sources folder are all a creation needs).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module ui.pipeline;

import foundation.core;
import foundation.content;
import pipeline.core;

using namespace foundation::core;

namespace pipeline
{
    void RegisterUICreators(AssetCreatorRegistry& registry)
    {
        // A UI document and a UI theme write their starter text to a LINKED source file in
        // Sources/ (.sml, .sss) that the asset references through fileName, like a script.
        {
            AssetCreator creator;
            creator.label = String(u8"UI Document");
            creator.category = String(u8"UI");
            creator.type = &UIDocumentAsset::StaticType();
            creator.run = [](const AssetCreationContext& context) -> foundation::content::Instance*
            {
                UIDocumentAsset asset;
                return CreateLinkedTextAsset(context, u8"UIDocument", u8".sml", kUIDocumentStarter,
                                             UIDocumentAsset::StaticType(), asset);
            };
            registry.Register(Move(creator));
        }
        {
            AssetCreator creator;
            creator.label = String(u8"UI Theme");
            creator.category = String(u8"UI");
            creator.type = &UIThemeAsset::StaticType();
            creator.run = [](const AssetCreationContext& context) -> foundation::content::Instance*
            {
                UIThemeAsset asset;
                return CreateLinkedTextAsset(context, u8"UITheme", u8".sss", kUIThemeStarter,
                                             UIThemeAsset::StaticType(), asset);
            };
            registry.Register(Move(creator));
        }
    }
}
