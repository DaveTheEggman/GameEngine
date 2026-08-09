// Source-side mesh cook: capture a primitive into a StaticMeshAsset, cook it through
// the builder into an output DB, and verify the cooked StaticMeshSource carries the
// vertex/index data. Plus the skinned path.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import pipeline.core;
import foundation.geometry;
import foundation.geometry.resource;
import geometry.pipeline;

using namespace foundation::core;
using namespace pipeline;
using namespace foundation::vfs;
using namespace foundation::geometry;

namespace
{
    void RemoveTree()
    {
        FileDelete(u8"scratch_mesh_edit_db/cube.rasset");
        FileDelete(u8"scratch_mesh_edit_db/skinned.rasset");
        RemoveDirectory(u8"scratch_mesh_edit_db");
    }
}

TEST_CASE("mesh editor: cooks a StaticMeshAsset -> StaticMeshSource")
{
    GlobalTypeRegistry().Register(StaticMeshSource::StaticType());
    RegisterSerializable<StaticMeshSource>();
    RegisterMeshAssets();

    RemoveTree();
    NativeFileSystem outMount(u8"scratch_mesh_edit_db");
    Guid id;

    {
        foundation::content::ContentDatabase outDb(
            outMount, foundation::core::BinarySerializerFactory(), u8".rasset");
        auto* inst = outDb.RootGroup()->CreateInstance(u8"cube", StaticMeshSource::StaticType());
        id = inst->Id();

        RefPtr<StaticMesh> cube = Primitives::Cube(1.0f);
        StaticMeshAsset asset;
        MeshImporter::Import(*cube, asset);

        StaticMeshAssetBuilder builder;
        REQUIRE(builder.AssetType() == &StaticMeshAsset::StaticType());
        pipeline::AssetBuildContext ctx;
        ctx.output = inst;
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    {
        foundation::content::ContentDatabase outDb(
            outMount, foundation::core::BinarySerializerFactory(), u8".rasset");
        RefPtr<ISerializable> object = outDb.ReadObject(id);
        StaticMeshSource* cooked = Cast<StaticMeshSource>(object.Get());
        REQUIRE(cooked != nullptr);
        CHECK(cooked->vertexBlob.Size() == 24 * sizeof(StaticMeshVertex));
        CHECK(cooked->indexData.Size() == 36);
        CHECK(cooked->subStart.Size() == 1);
    }

    RemoveTree();
}

TEST_CASE("mesh editor: cooks a SkinnedMeshAsset -> SkinnedMeshSource")
{
    GlobalTypeRegistry().Register(SkinnedMeshSource::StaticType());
    RegisterSerializable<SkinnedMeshSource>();
    RegisterMeshAssets();

    RemoveTree();
    NativeFileSystem outMount(u8"scratch_mesh_edit_db");
    Guid id;

    {
        foundation::content::ContentDatabase outDb(
            outMount, foundation::core::BinarySerializerFactory(), u8".rasset");
        auto* inst =
            outDb.RootGroup()->CreateInstance(u8"skinned", SkinnedMeshSource::StaticType());
        id = inst->Id();

        RefPtr<SkinnedMesh> mesh = MakeRef<SkinnedMesh>(DefaultAllocator());
        mesh->skeletonIndex = 4;
        mesh->vertices.PushBack(StaticMeshVertex{});
        mesh->vertices.PushBack(StaticMeshVertex{});
        VertexSkinning s{};
        s.joints[1] = 9;
        mesh->skinning.PushBack(s);
        mesh->skinning.PushBack(s);

        SkinnedMeshAsset asset;
        MeshImporter::Import(*mesh, asset);

        SkinnedMeshAssetBuilder builder;
        pipeline::AssetBuildContext ctx;
        ctx.output = inst;
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    {
        foundation::content::ContentDatabase outDb(
            outMount, foundation::core::BinarySerializerFactory(), u8".rasset");
        RefPtr<ISerializable> object = outDb.ReadObject(id);
        SkinnedMeshSource* cooked = Cast<SkinnedMeshSource>(object.Get());
        REQUIRE(cooked != nullptr);
        CHECK(cooked->vertexBlob.Size() == 2 * sizeof(StaticMeshVertex));
        CHECK(cooked->skinningBlob.Size() == 2 * sizeof(VertexSkinning));
        CHECK(cooked->skeletonIndex == 4);
    }

    RemoveTree();
}
