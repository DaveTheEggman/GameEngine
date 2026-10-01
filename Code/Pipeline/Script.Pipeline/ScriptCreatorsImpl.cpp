// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Script.Pipeline - the script domain's New Asset creators
// (agent-playtesting-and-asset-creation.md P1): one per tier (Behavior, Level, Game) for every
// registered script backend that has a cook, each seeded from the cook's tier starter (never
// hard-coded text) written to the sources folder.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module script.pipeline;

import foundation.core;
import foundation.content;
import foundation.script;
import pipeline.core;

using namespace foundation::core;

namespace pipeline
{
    foundation::content::Instance* CreateScriptInstance(const AssetCreationContext& context,
                                                        StringView languageId,
                                                        StringView extension, ScriptTier tier,
                                                        StringView baseName)
    {
        IScriptLanguageCook* cook = ScriptLanguageCookRegistry::Get().FindByLanguage(languageId);
        if (cook == nullptr)
        {
            return nullptr;
        }
        String dottedExtension(u8".");
        dottedExtension.Append(extension);
        ScriptClassAsset asset;
        asset.language = String(languageId);
        return CreateLinkedTextAsset(context, baseName, dottedExtension.AsView(),
                                     cook->NewAssetTemplate(tier), ScriptClassAsset::StaticType(),
                                     asset);
    }

    usize RegisterScriptCreators(AssetCreatorRegistry& registry)
    {
        struct TierDesc
        {
            ScriptTier tier;
            StringView suffix;   // the label's suffix
            StringView baseName; // the unique-name stem
        };
        static const TierDesc kTiers[] = {
            {ScriptTier::Behavior, u8"Behavior", u8"NewBehavior"},
            {ScriptTier::Level, u8"Level", u8"NewLevel"},
            {ScriptTier::Game, u8"Game", u8"NewGame"},
        };
        usize registered = 0;
        for (const foundation::script::ScriptBackendDesc& backend :
             foundation::script::ScriptBackendRegistry::Get().All())
        {
            // A backend with no cook (compile and harvest) cannot seed a starter.
            if (ScriptLanguageCookRegistry::Get().FindByLanguage(backend.languageId.AsView()) ==
                nullptr)
            {
                continue;
            }
            const String extension = backend.fileExtensions.IsEmpty()
                                         ? String(backend.languageId.AsView())
                                         : String(backend.fileExtensions[0].AsView());
            const StringView displayName = backend.displayName.IsEmpty()
                                               ? backend.languageId.AsView()
                                               : backend.displayName.AsView();
            for (const TierDesc& t : kTiers)
            {
                AssetCreator creator;
                creator.label = Format(u8"{} {}", displayName, t.suffix);
                creator.category = String(u8"Scripts");
                creator.type = &ScriptClassAsset::StaticType();
                creator.run = [languageId = String(backend.languageId.AsView()), extension,
                               tier = t.tier, base = String(t.baseName)](
                                  const AssetCreationContext& context)
                {
                    return CreateScriptInstance(context, languageId.AsView(), extension.AsView(),
                                                tier, base.AsView());
                };
                registry.Register(Move(creator));
                ++registered;
            }
        }
        return registered;
    }
}
