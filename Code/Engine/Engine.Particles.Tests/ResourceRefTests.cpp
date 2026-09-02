// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Resource-reference layer: ParticleEffectComponent's effectAsset resource::Ref round-trips
// through scene serialization by Guid, resolves through the ResourceManager's proxy handles,
// and the manager clones the cooked effect into a live instance on the next tick.

#include <atomic> // gcc modules: pull in std::atomic's always_inline bodies before any import
                  // (this import mix otherwise fails with "function body not available")
#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.particles;
import foundation.particles.resource;
import engine.particles;
import foundation.scene;
import foundation.scene.resource;

using namespace foundation::core;
using namespace engine::particles;
namespace scene = foundation::scene;
namespace resource = foundation::resource;
namespace particles = foundation::particles;

namespace
{
    void RemoveTree(StringView root)
    {
        foundation::vfs::NativeFileSystem fs(root, foundation::core::DefaultAllocator());
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

TEST_CASE("resource-ref: scene round-trip resolves the effect ref and the manager attaches it")
{
    const StringView dir = u8"scratch_pfxref_test_db";
    RemoveTree(dir);
    (void)CreateDirectory(dir);
    foundation::vfs::NativeFileSystem mount(dir, foundation::core::DefaultAllocator());

    particles::RegisterParticleEffectResource();

    // Cook an effect with one 64-particle system into the content DB.
    foundation::content::ContentDatabase cookedDb(foundation::core::DefaultAllocator(), mount, BinarySerializerFactory(), u8".rasset");
    Guid effectId;
    {
        particles::ParticleEffectResource resource;
        particles::ParticleSystem& sys = resource.Effect().AddSystem(64);
        sys.emitter.isEmitting = true;
        foundation::content::Instance* inst = cookedDb.RootGroup()->CreateInstance(
            u8"Puff", particles::ParticleEffectResource::StaticType());
        REQUIRE(inst != nullptr);
        REQUIRE(inst->WriteObject(resource).IsOk());
        effectId = inst->Id();
    }

    resource::ResourceManager resources(DefaultAllocator(), cookedDb);
    particles::ParticleEffectFactory factory;
    resources.AddFactory(&factory);

    // Author a scene whose ParticleEffectComponent references the effect BY GUID only.
    MemoryStream blob;
    {
        scene::Scene scene;
        scene.AddSystem<engine::particles::ParticleEffectComponentManager>();
        const scene::EntityHandle e = scene.CreateEntity(u8"Emitter");
        engine::particles::ParticleEffectComponent& c =
            scene.GetSystem<engine::particles::ParticleEffectComponentManager>()->Add(e);
        c.effectAsset.SetId(effectId);
        c.lightRange = 7.0f;

        BinarySerializer ar(blob, SerializeMode::Write);
        scene::SerializeScene(ar, scene);
        REQUIRE(ar.IsOk());
    }

    scene::Scene loaded;
    loaded.AddSystem<engine::particles::ParticleEffectComponentManager>();
    REQUIRE(blob.Seek(0, SeekOrigin::Begin) == 0);
    {
        BinarySerializer ar(blob, SerializeMode::Read);
        scene::SerializeScene(ar, loaded);
        REQUIRE(ar.IsOk());
    }

    auto* mgr = loaded.GetSystem<engine::particles::ParticleEffectComponentManager>();
    REQUIRE(mgr != nullptr);
    REQUIRE(mgr->ComponentCount() == 1u);
    engine::particles::ParticleEffectComponent* c = nullptr;
    mgr->ForEach([&](engine::particles::ParticleEffectComponent& pc, scene::EntityHandle) { c = &pc; });
    REQUIRE(c != nullptr);
    CHECK(c->effectAsset.id == effectId);
    CHECK(c->effectAsset.Get() == nullptr);
    CHECK(c->lightRange == doctest::Approx(7.0f));

    scene::ResolveSceneResources(loaded, resources);
    particles::ParticleEffectResource* live = c->effectAsset.Get();
    REQUIRE(live != nullptr);
    CHECK(live->Effect().SystemCount() == 1);

    // The manager attaches (clones + instantiates) on the next tick: the component gets its OWN
    // effect clone, not the shared cooked template.
    CHECK(c->instance.Get() == nullptr);
    loaded.Update(1.0f / 60.0f);
    REQUIRE(c->instance.Get() != nullptr);
    REQUIRE(c->ownedEffect.Get() != nullptr);
    CHECK(c->ownedEffect.Get() != &live->Effect());
    CHECK(c->ownedEffect->SystemCount() == 1);
    CHECK(c->attachedResource == live);

    RemoveTree(dir);
}

TEST_CASE("resource-ref: SetEffect(proxy) still attaches immediately (sample path)")
{
    const StringView dir = u8"scratch_pfxref_test_db2";
    RemoveTree(dir);
    (void)CreateDirectory(dir);
    foundation::vfs::NativeFileSystem mount(dir, foundation::core::DefaultAllocator());

    particles::RegisterParticleEffectResource();
    foundation::content::ContentDatabase cookedDb(foundation::core::DefaultAllocator(), mount, BinarySerializerFactory(), u8".rasset");
    Guid effectId;
    {
        particles::ParticleEffectResource resource;
        (void)resource.Effect().AddSystem(16);
        foundation::content::Instance* inst = cookedDb.RootGroup()->CreateInstance(
            u8"Spark", particles::ParticleEffectResource::StaticType());
        REQUIRE(inst != nullptr);
        REQUIRE(inst->WriteObject(resource).IsOk());
        effectId = inst->Id();
    }
    resource::ResourceManager resources(DefaultAllocator(), cookedDb);
    particles::ParticleEffectFactory factory;
    resources.AddFactory(&factory);

    engine::particles::ParticleEffectComponent c;
    c.SetEffect(resources.Bind<particles::ParticleEffectResource>(effectId));
    REQUIRE(c.instance.Get() != nullptr);
    REQUIRE(c.effectAsset.Get() != nullptr);
    CHECK(c.attachedResource == c.effectAsset.Get());

    RemoveTree(dir);
}

TEST_CASE("particles: entity-active - starts-inactive never attaches/emits; toggle freezes the sim")
{
    particles::RegisterParticleEffectResource();

    scene::Scene sceneObj;
    sceneObj.AddSystem<engine::particles::ParticleEffectComponentManager>();
    const scene::EntityHandle e = sceneObj.CreateEntity(u8"Emitter");
    engine::particles::ParticleEffectComponent& c =
        sceneObj.GetSystem<engine::particles::ParticleEffectComponentManager>()->Add(e);

    RefPtr<particles::ParticleEffectResource> res =
        MakeRef<particles::ParticleEffectResource>(DefaultAllocator());
    particles::ParticleSystem& sys = res->Effect().AddSystem(64);
    sys.emitter.isEmitting = true;
    sys.emitter.spawnRate = 100.0f;
    sys.AddInitializer<particles::LifetimeInitializer>().lifetime =
        particles::RangeFloat{10.0f, 10.0f}; // long-lived so alive counts are stable
    c.effectAsset = res.Get();

    // Starts inactive: the manager never attaches, nothing emits.
    sceneObj.SetActive(e, false);
    sceneObj.Update(0.1f);
    sceneObj.Update(0.1f);
    CHECK(c.instance.Get() == nullptr);

    // Activation: attaches + simulates.
    sceneObj.SetActive(e, true);
    sceneObj.Update(0.1f); // attach tick
    REQUIRE(c.instance.Get() != nullptr);
    sceneObj.Update(0.2f);
    const i32 alive = c.instance->Effect().GetSystem(0)->AliveCount();
    CHECK(alive > 0);

    // Deactivate: live particles FREEZE (no sim, no emission, no decay - v1).
    sceneObj.SetActive(e, false);
    sceneObj.Update(0.5f);
    CHECK(c.instance->Effect().GetSystem(0)->AliveCount() == alive);
}
