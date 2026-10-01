// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Engine.Composition.Tests - scene export support: the scene stream transcode and the scene
// reference scan every exporting host shares, both over the full manager set.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.scene;
import foundation.scene.resource;
import engine.composition;
import engine.render;
using namespace foundation::core;
namespace scene = foundation::scene;
namespace content = foundation::content;

namespace
{
    constexpr StringView kRoot = u8"scratch_scene_export_support";

    void RemoveTree() { (void)RemoveDirectoryRecursive(kRoot); }
}

TEST_CASE("engine.composition: a scene's references and its stream come from the full manager set")
{
    engine::RegisterAllSceneComponentReflection();
    engine::RegisterAllResourceTypes();
    RemoveTree();
    foundation::vfs::NativeFileSystem mount(kRoot, DefaultAllocator());
    content::ContentDatabase db(DefaultAllocator(), mount, BinarySerializerFactory(), u8".rasset");

    // A level whose one entity draws a mesh: the reference the scan must report.
    const Guid meshId(StringView(u8"5b1c0de4-2f3a-4c6e-9d10-7a8b9c0d1e2f"));
    content::Instance* level =
        db.RootGroup()->CreateInstance(u8"level", scene::SceneDocument::StaticType());
    REQUIRE(level != nullptr);
    {
        scene::Scene author(DefaultAllocator(), u8"level");
        engine::AddAllSceneManagers(author);
        const scene::EntityHandle entity = author.CreateEntity(u8"rock");
        author.GetSystem<engine::render::MeshComponentManager>()->Add(entity).mesh.SetId(meshId);
        REQUIRE(scene::SaveScene(author, *level).IsOk());
    }

    CHECK(engine::IsSceneLike(*level));

    Array<Guid> resources;
    Array<Guid> prefabs;
    REQUIRE(engine::ScanSceneReferences(DefaultAllocator(), *level, db, resources, prefabs));
    REQUIRE(resources.Size() == 1u);
    CHECK(resources[0] == meshId);
    CHECK(prefabs.IsEmpty());

    HashMap<Guid, Array<byte>> streams;
    engine::CollectSceneStreams(DefaultAllocator(), *db.RootGroup(), streams);
    CHECK(streams.Size() == 1u);
    const Array<byte>* binary = streams.Find(level->Id());
    REQUIRE(binary != nullptr);
    CHECK(!binary->IsEmpty());

    RemoveTree();
}
