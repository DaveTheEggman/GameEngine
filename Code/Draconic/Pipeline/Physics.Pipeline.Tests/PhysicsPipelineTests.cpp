// Full collision pipeline: author a mesh (source db) -> CollisionShapeAsset -> cook via
// the builder into an output db -> load the CollisionShape product through the factory ->
// hand its blob to a live PhysicsWorld. Same for PhysicalMaterial.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include <initializer_list>

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import pipeline.core;
import foundation.geometry;
import geometry.pipeline;
import foundation.geometry.resource;
import foundation.physics;
import foundation.physics.resource;
import physics.pipeline;

using namespace foundation::core;
using namespace pipeline;
using namespace foundation::resource;
using namespace foundation::physics;
namespace geometry = foundation::geometry;
namespace content = foundation::content;

namespace
{
    void RemoveTree(StringView dir)
    {
        Array<String> names;
        // Content DBs write flat files; nuke known extensions then the dir.
        for (const utf8char* f : {u8"cube.rasset", u8"shape.rasset", u8"surface.rasset"})
        {
            String path(dir);
            path.Append(u8"/");
            path.Append(f);
            FileDelete(path.AsView());
        }
        RemoveDirectory(dir);
    }
}

TEST_CASE("physics.pipeline: mesh -> CollisionShapeAsset cook -> CollisionShape -> live body")
{
    RegisterPhysicsResource();
    RegisterPhysicsAssets();
    pipeline::RegisterMeshAssets();
    RemoveTree(u8"scratch_physpipe_src_db");
    RemoveTree(u8"scratch_physpipe_out_db");

    foundation::vfs::NativeFileSystem srcMount(u8"scratch_physpipe_src_db");
    foundation::vfs::NativeFileSystem outMount(u8"scratch_physpipe_out_db");
    content::ContentDatabase srcDb(srcMount, BinarySerializerFactory(), u8".rasset");
    content::ContentDatabase outDb(outMount, BinarySerializerFactory(), u8".rasset");

    // Source mesh asset: a unit cube captured into a StaticMeshSource.
    auto* meshInstance =
        srcDb.RootGroup()->CreateInstance(u8"cube", pipeline::StaticMeshAsset::StaticType());
    {
        pipeline::StaticMeshAsset meshAsset;
        RefPtr<geometry::StaticMesh> cube = geometry::Primitives::Cube(1.0f);
        geometry::StaticMeshSource::FromMesh(*cube, meshAsset.source);
        REQUIRE(meshInstance->WriteObject(meshAsset).IsOk());
    }

    // Both cook kinds produce a loadable shape that simulates.
    for (const CollisionCookKind kind :
         {CollisionCookKind::ConvexHull, CollisionCookKind::TriangleMesh})
    {
        CollisionShapeAsset asset;
        asset.sourceMesh = meshInstance->Id();
        asset.cook = kind;

        // Dependencies: the mesh guid must be declared as a hash-chained read.
        CollisionShapeAssetBuilder builder;
        pipeline::AssetBuildContext ctx;
        ctx.sources = &srcMount;
        ctx.db = &srcDb;
        pipeline::AssetDependencies deps;
        builder.ScanDependencies(asset, ctx, deps);
        REQUIRE(deps.reads.Size() == 1);
        CHECK(deps.reads[0] == meshInstance->Id());

        auto* outInstance =
            outDb.RootGroup()->GetInstance(u8"shape") != nullptr
                ? outDb.RootGroup()->GetInstance(u8"shape")
                : outDb.RootGroup()->CreateInstance(u8"shape", CollisionShapeSource::StaticType());
        ctx.output = outInstance;
        REQUIRE(builder.Build(asset, ctx).IsOk());

        // Runtime load through the factory.
        CollisionShapeFactory factory;
        ResourceManager manager(outDb);
        manager.AddFactory(&factory);
        Proxy<CollisionShape> shape = manager.Bind<CollisionShape>(outInstance->Id());
        REQUIRE(shape);
        CHECK(shape->convex == (kind == CollisionCookKind::ConvexHull));
        REQUIRE(!shape->blob.IsEmpty());
        REQUIRE(!shape->outline.IsEmpty());
        CHECK(shape->outline.Size() % 3 == 0);

        // The blob drives a real body. Meshes must be static; hulls may fall.
        PhysicsWorld world;
        BodyDesc desc;
        desc.motion =
            kind == CollisionCookKind::ConvexHull ? MotionKind::Dynamic : MotionKind::Static;
        desc.layer =
            desc.motion == MotionKind::Static ? PhysicsLayer::Static : PhysicsLayer::Dynamic;
        ShapeDesc sd;
        sd.kind = ShapeKind::Cooked;
        sd.cooked = shape->Blob();
        desc.shapes.PushBack(sd);
        CHECK(world.CreateBody(desc).IsValid());
    }

    RemoveTree(u8"scratch_physpipe_src_db");
    RemoveTree(u8"scratch_physpipe_out_db");
}

// Regression (issues-triage #7 / smoke #7): the real editor cook resolves the mesh `reads` edge to
// the mesh's cooked PRODUCT - a StaticMeshSource - not the source StaticMeshAsset. The builder must
// cook from the product; when it only Cast<StaticMeshAsset> it failed every generate-collision cook
// with "source is not a mesh asset". Feed the guid a StaticMeshSource exactly as the db holds it
// after the mesh cooks and before the collision cook reads it.
TEST_CASE("physics.pipeline: collision cooks from the mesh PRODUCT (StaticMeshSource)")
{
    RegisterPhysicsResource();
    RegisterPhysicsAssets();
    GlobalTypeRegistry().Register(geometry::StaticMeshSource::StaticType());
    RegisterSerializable<geometry::StaticMeshSource>(); // product type for ReadObject
    RemoveTree(u8"scratch_physpipe_prod_db");
    RemoveTree(u8"scratch_physpipe_prodout_db");

    foundation::vfs::NativeFileSystem prodMount(u8"scratch_physpipe_prod_db");
    foundation::vfs::NativeFileSystem outMount(u8"scratch_physpipe_prodout_db");
    content::ContentDatabase prodDb(prodMount, BinarySerializerFactory(), u8".rasset");
    content::ContentDatabase outDb(outMount, BinarySerializerFactory(), u8".rasset");

    auto* meshInstance =
        prodDb.RootGroup()->CreateInstance(u8"cube", geometry::StaticMeshSource::StaticType());
    {
        geometry::StaticMeshSource meshSource;
        RefPtr<geometry::StaticMesh> cube = geometry::Primitives::Cube(1.0f);
        geometry::StaticMeshSource::FromMesh(*cube, meshSource);
        REQUIRE(meshInstance->WriteObject(meshSource).IsOk());
    }

    CollisionShapeAsset asset;
    asset.sourceMesh = meshInstance->Id();
    asset.cook = CollisionCookKind::ConvexHull;

    CollisionShapeAssetBuilder builder;
    pipeline::AssetBuildContext ctx;
    ctx.sources = &prodMount;
    ctx.db = &prodDb;
    auto* outInstance =
        outDb.RootGroup()->CreateInstance(u8"shape", CollisionShapeSource::StaticType());
    ctx.output = outInstance;
    REQUIRE(builder.Build(asset, ctx).IsOk()); // was Status::InvalidArgument before the fix

    CollisionShapeFactory factory;
    ResourceManager manager(outDb);
    manager.AddFactory(&factory);
    Proxy<CollisionShape> shape = manager.Bind<CollisionShape>(outInstance->Id());
    REQUIRE(shape);
    CHECK(shape->convex);
    REQUIRE(!shape->blob.IsEmpty());

    RemoveTree(u8"scratch_physpipe_prod_db");
    RemoveTree(u8"scratch_physpipe_prodout_db");
}

TEST_CASE("physics.pipeline: PhysicalMaterialAsset cooks and loads")
{
    RegisterPhysicsResource();
    RegisterPhysicsAssets();
    RemoveTree(u8"scratch_physpipe_mat_db");

    foundation::vfs::NativeFileSystem outMount(u8"scratch_physpipe_mat_db");
    content::ContentDatabase outDb(outMount, BinarySerializerFactory(), u8".rasset");
    auto* instance =
        outDb.RootGroup()->CreateInstance(u8"surface", PhysicalMaterialSource::StaticType());

    PhysicalMaterialAsset asset;
    asset.friction = 0.9f;
    asset.restitution = 0.25f;
    asset.density = 2500.0f;
    PhysicalMaterialAssetBuilder builder;
    pipeline::AssetBuildContext ctx;
    ctx.output = instance;
    REQUIRE(builder.Build(asset, ctx).IsOk());

    PhysicalMaterialFactory factory;
    ResourceManager manager(outDb);
    manager.AddFactory(&factory);
    Proxy<PhysicalMaterial> material = manager.Bind<PhysicalMaterial>(instance->Id());
    REQUIRE(material);
    CHECK(material->friction == doctest::Approx(0.9f));
    CHECK(material->restitution == doctest::Approx(0.25f));
    CHECK(material->density == doctest::Approx(2500.0f));

    RemoveTree(u8"scratch_physpipe_mat_db");
}
