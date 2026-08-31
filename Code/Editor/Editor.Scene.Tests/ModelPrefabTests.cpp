// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Model->prefab generation: a hand-authored model manifest becomes a spawnable
// PrefabDocument whose hierarchy + mesh/material/animation refs mirror the manifest, and a
// second generation REUSES the prefab instance (same guid - re-import propagates to placed
// instances through the standard rebuild machinery).

#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.scene;
import foundation.scene.resource;
import engine.render;
import engine.animation;
import modelimporter;
import editor.scene;

using namespace foundation::core;
namespace scene = foundation::scene;

namespace
{
    void RemoveTreeMP(StringView root)
    {
        foundation::vfs::NativeFileSystem fs(root);
        Array<foundation::vfs::DirEntry> entries;
        if (fs.AsEnumerable()->Enumerate(u8"", entries).IsOk())
        {
            for (const auto& e : entries)
            {
                if (!e.isDirectory)
                {
                    (void)fs.AsWritable()->Delete(e.name.AsView());
                    continue;
                }
                // One level of group subdirectories (the model group) - a stale "Prefab"
                // instance in there flips the regeneration check on reruns.
                Array<foundation::vfs::DirEntry> inner;
                if (fs.AsEnumerable()->Enumerate(e.name.AsView(), inner).IsOk())
                {
                    for (const auto& f : inner)
                    {
                        String path = PathJoin(e.name.AsView(), f.name.AsView());
                        (void)fs.AsWritable()->Delete(path.AsView());
                    }
                }
                (void)fs.AsWritable()->Delete(e.name.AsView());
            }
        }
        (void)RemoveDirectory(root);
    }
}

TEST_CASE("model-prefab: manifest -> spawnable prefab; regeneration reuses the instance")
{
    pipeline::RegisterModelManifestAsset();
    // Component reflection carries the DataVersion the versioned payloads gate on (MeshComponent v3
    // = unified materials array); register it so the round-trip is order-independent.
    engine::render::RegisterRenderComponentReflection();
    engine::animation::RegisterAnimationComponentReflection();
    GlobalTypeRegistry().Register(scene::PrefabDocument::StaticType());
    RegisterSerializable<scene::PrefabDocument>();

    const StringView dir = u8"scratch_model_prefab_test_db";
    RemoveTreeMP(dir);
    (void)CreateDirectory(dir);
    foundation::vfs::NativeFileSystem mount(dir);
    foundation::content::ContentDatabase db(mount, BinarySerializerFactory(), u8".rasset");

    // Hand-authored manifest: root node + a multi-material static mesh node + a skinned node.
    const Guid meshStatic{0x51, 0x1};
    const Guid meshSkinned{0x52, 0x2};
    const Guid matA{0x61, 0x1};
    const Guid matB{0x62, 0x2};
    const Guid skeleton{0x71, 0x1};
    const Guid clip{0x72, 0x1};

    pipeline::ModelManifestAsset asset;
    asset.manifest.meshGuids.PushBack(meshStatic);
    asset.manifest.meshGuids.PushBack(meshSkinned);
    asset.manifest.meshSkinned.PushBack(0);
    asset.manifest.meshSkinned.PushBack(1);
    asset.manifest.meshMaterial.PushBack(1); // static mesh's first part uses material B
    asset.manifest.meshMaterial.PushBack(0);
    asset.manifest.materialGuids.PushBack(matA);
    asset.manifest.materialGuids.PushBack(matB);
    asset.manifest.skeletonGuid = skeleton;
    asset.manifest.animationGuids.PushBack(clip);
    {
        foundation::model::ModelNode rootNode;
        rootNode.name = String(u8"Armature");
        rootNode.parentIndex = -1;
        asset.manifest.nodes.PushBack(Move(rootNode));
        foundation::model::ModelNode meshNode;
        meshNode.name = String(u8"Body");
        meshNode.parentIndex = 0;
        meshNode.meshIndex = 0;
        meshNode.localTransform.position = Float3{1.0f, 2.0f, 3.0f};
        asset.manifest.nodes.PushBack(Move(meshNode));
        foundation::model::ModelNode skinNode;
        skinNode.name = String(u8"Skin");
        skinNode.parentIndex = 0;
        skinNode.meshIndex = 1;
        asset.manifest.nodes.PushBack(Move(skinNode));
    }

    foundation::content::Group* group = db.RootGroup()->CreateGroup(u8"Fox");
    REQUIRE(group != nullptr);
    foundation::content::Instance* manifestInst =
        group->CreateInstance(u8"Fox", pipeline::ModelManifestAsset::StaticType());
    REQUIRE(manifestInst != nullptr);
    REQUIRE(manifestInst->WriteObject(asset).IsOk());

    editor::ModelPrefabResult generated =
        editor::GenerateModelPrefab(*manifestInst);
    REQUIRE(generated.instance != nullptr);
    CHECK(!generated.regenerated);
    CHECK(generated.instance->Name() == StringView(u8"Prefab"));
    CHECK(generated.instance->TypeName() == StringView(u8"PrefabDocument"));
    const Guid prefabId = generated.instance->Id();

    // Spawn the payload: hierarchy + refs mirror the manifest.
    UniquePtr<IStream> payload = generated.instance->ReadData(u8"scene");
    REQUIRE(payload.Get() != nullptr);
    scene::Scene level(u8"level");
    auto* meshes = level.AddSystem<engine::render::MeshComponentManager>();
    auto* anims = level.AddSystem<engine::animation::SkeletalAnimationComponentManager>();
    scene::EntityHandle root = scene::SpawnPrefab(level, *payload, prefabId);
    REQUIRE(root.IsAssigned());
    CHECK(level.GetEntityName(root) == StringView(u8"Fox"));

    scene::EntityHandle armature = level.GetFirstChild(root);
    REQUIRE(armature.IsAssigned());
    CHECK(level.GetEntityName(armature) == StringView(u8"Armature"));

    usize meshCount = 0;
    bool sawStatic = false, sawSkinned = false;
    meshes->ForEach(
        [&](engine::render::MeshComponent& c, scene::EntityHandle e)
        {
            ++meshCount;
            if (c.mesh.id == meshStatic)
            {
                sawStatic = true;
                REQUIRE(c.materials.Size() == 2u); // the unified material list
                CHECK(c.materials[0].id == matA);
                CHECK(c.materials[1].id == matB);
                const Transform t = level.GetLocalTransform(e);
                CHECK(t.position.x == 1.0f);
            }
            if (c.mesh.id == meshSkinned)
            {
                sawSkinned = true;
                REQUIRE(c.materials.Size() == 2u);
            }
        });
    CHECK(meshCount == 2u);
    CHECK(sawStatic);
    CHECK(sawSkinned);

    usize animCount = 0;
    anims->ForEach(
        [&](engine::animation::SkeletalAnimationComponent& c, scene::EntityHandle e)
        {
            ++animCount;
            CHECK(c.skeleton.id == skeleton);
            CHECK(c.clip.id == clip);
            // ONE animator on the prefab ROOT feeds the skinned nodes through
            // prefab-remapped EntityRefs - never one animator per part.
            CHECK(e == root);
            REQUIRE(c.meshEntities.Size() == 1u);
            const scene::EntityHandle fed = level.FindEntity(c.meshEntities[0].id);
            REQUIRE(fed.IsAssigned());
            engine::render::MeshComponent* fedMesh = meshes->Get(fed);
            REQUIRE(fedMesh != nullptr);
            CHECK(fedMesh->mesh.id == meshSkinned);
        });
    CHECK(animCount == 1u); // one animator for the whole model

    // Regeneration finds + reuses the instance: same guid, refreshed payload.
    editor::ModelPrefabResult again =
        editor::GenerateModelPrefab(*manifestInst);
    REQUIRE(again.instance != nullptr);
    CHECK(again.regenerated);
    CHECK(again.instance->Id() == prefabId);

    RemoveTreeMP(dir);
}

TEST_CASE("model-scene: manifest -> standalone scene; regeneration reuses the instance")
{
    pipeline::RegisterModelManifestAsset();
    // Component reflection carries the DataVersion the versioned payloads gate on (MeshComponent v3
    // = unified materials array); register it so the materials round-trip is order-independent.
    engine::render::RegisterRenderComponentReflection();
    engine::animation::RegisterAnimationComponentReflection();
    GlobalTypeRegistry().Register(scene::SceneDocument::StaticType());
    RegisterSerializable<scene::SceneDocument>();

    const StringView dir = u8"scratch_model_scene_test_db";
    RemoveTreeMP(dir);
    (void)CreateDirectory(dir);
    foundation::vfs::NativeFileSystem mount(dir);
    foundation::content::ContentDatabase db(mount, BinarySerializerFactory(), u8".rasset");

    // Root node + a single static-mesh child node (Crate -> Root -> Body).
    const Guid meshStatic{0x51, 0x1};
    const Guid matA{0x61, 0x1};
    pipeline::ModelManifestAsset asset;
    asset.manifest.meshGuids.PushBack(meshStatic);
    asset.manifest.meshSkinned.PushBack(0);
    asset.manifest.materialGuids.PushBack(matA);
    {
        foundation::model::ModelNode rootNode;
        rootNode.name = String(u8"Root");
        rootNode.parentIndex = -1;
        asset.manifest.nodes.PushBack(Move(rootNode));
        foundation::model::ModelNode meshNode;
        meshNode.name = String(u8"Body");
        meshNode.parentIndex = 0;
        meshNode.meshIndex = 0;
        meshNode.localTransform.position = Float3{4.0f, 5.0f, 6.0f};
        asset.manifest.nodes.PushBack(Move(meshNode));
    }

    foundation::content::Group* group = db.RootGroup()->CreateGroup(u8"Crate");
    REQUIRE(group != nullptr);
    foundation::content::Instance* manifestInst =
        group->CreateInstance(u8"Crate", pipeline::ModelManifestAsset::StaticType());
    REQUIRE(manifestInst != nullptr);
    REQUIRE(manifestInst->WriteObject(asset).IsOk());

    editor::ModelPrefabResult generated = editor::GenerateModelScene(*manifestInst);
    REQUIRE(generated.instance != nullptr);
    CHECK(!generated.regenerated);
    CHECK(generated.instance->Name() == StringView(u8"Scene"));
    CHECK(generated.instance->TypeName() == StringView(u8"SceneDocument"));
    const Guid sceneId = generated.instance->Id();

    // The SceneDocument primary carries the name for discovery.
    RefPtr<ISerializable> doc = generated.instance->ReadObject();
    scene::SceneDocument* sd = Cast<scene::SceneDocument>(doc.Get());
    REQUIRE(sd != nullptr);
    CHECK(sd->name == StringView(u8"Crate"));

    // Load it back into a fresh scene whose managers are injected first (as a subsystem would):
    // the node hierarchy + mesh refs round-trip, a bare scene with NO default camera/light.
    scene::Scene loaded;
    auto* meshes = loaded.AddSystem<engine::render::MeshComponentManager>();
    loaded.AddSystem<engine::animation::SkeletalAnimationComponentManager>();
    REQUIRE(scene::LoadScene(*generated.instance, loaded).IsOk());
    CHECK(loaded.EntityCount() == 3u); // Crate root + Root + Body, nothing auto-added

    scene::EntityHandle sceneRoot = loaded.FindEntityByName(u8"Crate");
    REQUIRE(sceneRoot.IsAssigned());
    scene::EntityHandle body = loaded.FindEntityByName(u8"Body");
    REQUIRE(body.IsAssigned());
    const Transform bodyT = loaded.GetLocalTransform(body);
    CHECK(bodyT.position.x == 4.0f);

    usize meshCount = 0;
    meshes->ForEach(
        [&](engine::render::MeshComponent& c, scene::EntityHandle)
        {
            ++meshCount;
            CHECK(c.mesh.id == meshStatic);
            REQUIRE(c.materials.Size() == 1u);
            CHECK(c.materials[0].id == matA);
        });
    CHECK(meshCount == 1u);

    // Regeneration finds + reuses the instance: same guid, refreshed payload.
    editor::ModelPrefabResult regen = editor::GenerateModelScene(*manifestInst);
    REQUIRE(regen.instance != nullptr);
    CHECK(regen.regenerated);
    CHECK(regen.instance->Id() == sceneId);

    RemoveTreeMP(dir);
}
