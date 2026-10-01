// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Pipeline::Scene - `scene.pipeline` (implementation): the scene and prefab creators.

module;
#include "Core/Prelude.h"

module scene.pipeline;

import foundation.core;
import foundation.content;
import foundation.scene;
import foundation.scene.resource;
import engine.render; // LightComponentManager: the new scene's sun
import pipeline.core;

using namespace foundation::core;
namespace scene = foundation::scene;

namespace pipeline
{
    foundation::content::Instance* CreateSceneInstance(foundation::content::Group* target,
                                                       StringView baseName, IAllocator& allocator)
    {
        if (target == nullptr)
        {
            return nullptr;
        }
        const String name = target->UniqueInstanceName(baseName);
        foundation::content::Instance* instance =
            target->CreateInstance(name.AsView(), scene::SceneDocument::StaticType());
        if (instance == nullptr)
        {
            return nullptr;
        }
        scene::SceneDocument doc;
        doc.name = name;
        if (!instance->WriteObject(doc).IsOk())
        {
            return nullptr;
        }
        // A directional sun, angled so the first mesh dropped in is lit and shadowed. The
        // intensity stays the component default, so the value a user tunes is the one shown.
        scene::Scene seeded(allocator, name.AsView());
        seeded.AddSystem<engine::render::LightComponentManager>();
        const scene::EntityHandle sun = seeded.CreateEntity(u8"Sun");
        Transform t;
        // Shines along the entity's forward (-Z): tilted about 60 degrees down, with a slight
        // compass yaw so shading has direction.
        t.rotation = Quaternion::FromAxisAngle(Float3{0, 1, 0}, 0.35f) *
                     Quaternion::FromAxisAngle(Float3{1, 0, 0}, -1.05f);
        seeded.SetLocalTransform(sun, t);
        engine::render::LightComponent& light =
            seeded.GetSystem<engine::render::LightComponentManager>()->Add(sun);
        light.castsShadows = true;
        (void)scene::SaveScene(seeded, *instance);
        return instance;
    }

    foundation::content::Instance* CreatePrefabInstance(foundation::content::Group* target,
                                                        StringView baseName, IAllocator& allocator)
    {
        if (target == nullptr)
        {
            return nullptr;
        }
        const String name = target->UniqueInstanceName(baseName);
        foundation::content::Instance* instance =
            target->CreateInstance(name.AsView(), scene::PrefabDocument::StaticType());
        if (instance == nullptr)
        {
            return nullptr;
        }
        scene::PrefabDocument doc;
        doc.name = name;
        if (!instance->WriteObject(doc).IsOk())
        {
            return nullptr;
        }
        scene::Scene seed(allocator, u8"seed");
        const scene::EntityHandle root = seed.CreateEntity(name.AsView());
        MemoryStream buffer;
        if (scene::CapturePrefab(seed, root, buffer).IsOk())
        {
            (void)instance->WriteData(u8"scene", buffer.Bytes());
        }
        return instance;
    }

    void RegisterSceneCreators(AssetCreatorRegistry& registry)
    {
        IAllocator* allocator = &registry.Allocator();
        {
            AssetCreator creator;
            creator.label = String(u8"Scene");
            creator.type = &scene::SceneDocument::StaticType();
            creator.defaultGroup = String(u8"Scenes");
            creator.setsDefaultScene = true;
            creator.run = [allocator](const AssetCreationContext& context)
            { return CreateSceneInstance(context.Target(), context.NameOr(u8"Scene"), *allocator); };
            registry.Register(Move(creator));
        }
        {
            AssetCreator creator;
            creator.label = String(u8"Prefab");
            creator.type = &scene::PrefabDocument::StaticType();
            creator.defaultGroup = String(u8"Prefabs");
            creator.run = [allocator](const AssetCreationContext& context)
            {
                return CreatePrefabInstance(context.Target(), context.NameOr(u8"Prefab"),
                                            *allocator);
            };
            registry.Register(Move(creator));
        }
    }
}
