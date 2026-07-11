// Resource-reference layer (asset-pipeline 6b): MeshComponent's resource::Ref fields
// round-trip through scene serialization by Guid and resolve through the ResourceManager's
// PROXY HANDLES - so a Reload swaps the product behind every holder without a re-resolve.

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.resource;
import draconic.geometry;
import draconic.geometry.resource;
import draconic.scene;
import draconic.scene.resource;
import draconic.render.subsystem;

using namespace draconic::core;
using namespace draconic::render;
namespace dscene = draconic::scene;
namespace res = draconic::resource;
namespace geo = draconic::geometry;

namespace
{
    void RemoveTree(StringView root)
    {
        draconic::vfs::NativeFileSystem fs(root);
        Array<draconic::vfs::DirEntry> entries;
        if (fs.AsEnumerable()->Enumerate(u8"", entries).IsOk())
        {
            for (const auto& e : entries)
            {
                if (!e.isDirectory) { (void)fs.AsWritable()->Delete(e.name.AsView()); }
            }
        }
        (void)RemoveDirectory(root);
    }
}

TEST_CASE("resource-ref: scene round-trip resolves mesh refs through proxy handles")
{
    const StringView dir = u8"draconic_resref_test_db";
    RemoveTree(dir);
    (void)CreateDirectory(dir);
    draconic::vfs::NativeFileSystem mount(dir);

    // A cooked mesh product: a unit cube baked into a StaticMeshSource instance.
    GlobalTypeRegistry().Register(geo::StaticMeshSource::StaticType());
    RegisterSerializable<geo::StaticMeshSource>();
    draconic::content::ContentDatabase cookedDb(mount, BinarySerializerFactory(), u8".rasset");
    Guid meshId;
    {
        RefPtr<geo::StaticMesh> cube = geo::Primitives::Cube(1.0f);
        geo::StaticMeshSource source;
        geo::StaticMeshSource::FromMesh(*cube, source);
        draconic::content::Instance* inst =
            cookedDb.RootGroup()->CreateInstance(u8"Cube", geo::StaticMeshSource::StaticType());
        REQUIRE(inst != nullptr);
        REQUIRE(inst->WriteObject(source).IsOk());
        meshId = inst->Id();
    }

    res::ResourceManager resources(cookedDb);
    geo::StaticMeshFactory meshFactory;
    resources.AddFactory(&meshFactory);

    // Author a scene whose MeshComponent references the mesh BY GUID only.
    MemoryStream blob;
    {
        dscene::Scene scene;
        scene.AddSystem<MeshComponentManager>();
        const dscene::EntityHandle e = scene.CreateEntity(u8"Box");
        MeshComponent& mc = scene.GetSystem<MeshComponentManager>()->Add(e);
        mc.mesh.SetId(meshId);
        mc.color = Color{ 0.5f, 0.25f, 0.125f, 1.0f };

        BinarySerializer ar(blob, SerializeMode::Write);
        dscene::SerializeScene(ar, scene);
        REQUIRE(ar.IsOk());
    }

    // Load into a FRESH scene, then run the post-load resolve pass.
    dscene::Scene loaded;
    loaded.AddSystem<MeshComponentManager>();
    REQUIRE(blob.Seek(0, SeekOrigin::Begin) == 0);
    {
        BinarySerializer ar(blob, SerializeMode::Read);
        dscene::SerializeScene(ar, loaded);
        REQUIRE(ar.IsOk());
    }

    auto* meshes = loaded.GetSystem<MeshComponentManager>();
    REQUIRE(meshes != nullptr);
    REQUIRE(meshes->ComponentCount() == 1u);
    MeshComponent* mc = nullptr;
    meshes->ForEach([&](MeshComponent& c, dscene::EntityHandle) { mc = &c; });
    REQUIRE(mc != nullptr);

    // Identity survived; the product isn't bound until the resolve pass runs.
    CHECK(mc->mesh.id == meshId);
    CHECK(mc->mesh.Get() == nullptr);
    CHECK(mc->color.r == doctest::Approx(0.5f));

    dscene::ResolveSceneResources(loaded, resources);
    geo::StaticMesh* live = mc->mesh.Get();
    REQUIRE(live != nullptr);
    CHECK(live->vertices.Size() > 0u);
    const f32 sizeBefore = live->bounds.max.x - live->bounds.min.x;
    CHECK(sizeBefore == doctest::Approx(1.0f));

    // PROXY semantics: rewrite the cooked source (a 2x cube), Reload, and the SAME ref sees
    // the new product through its handle - no re-resolve pass. (Pointer equality is not a
    // valid signal: the allocator may reuse the freed block.)
    {
        RefPtr<geo::StaticMesh> bigger = geo::Primitives::Cube(2.0f);
        geo::StaticMeshSource source;
        geo::StaticMeshSource::FromMesh(*bigger, source);
        REQUIRE(cookedDb.GetInstance(meshId)->WriteObject(source).IsOk());
    }
    REQUIRE(resources.Reload(meshId));
    geo::StaticMesh* reloaded = mc->mesh.Get();
    REQUIRE(reloaded != nullptr);
    const f32 sizeAfter = reloaded->bounds.max.x - reloaded->bounds.min.x;
    CHECK(sizeAfter == doctest::Approx(2.0f));

    RemoveTree(dir);
}

TEST_CASE("resource-ref: direct objects win over proxies and skip serialization")
{
    RefPtr<geo::StaticMesh> procedural = geo::Primitives::Cube(2.0f);

    MeshComponent mc;
    mc.mesh = procedural;   // the sample/procedural path: plain RefPtr assignment
    CHECK(mc.mesh.Get() == procedural.Get());
    CHECK(mc.mesh.id.IsNil());   // nothing to serialize - direct objects are runtime-only

    // A guid alongside a direct object: the direct object still wins at Get().
    Random rng(1234);
    mc.mesh.SetId(Guid::Generate(rng));
    CHECK(mc.mesh.Get() == procedural.Get());
}
