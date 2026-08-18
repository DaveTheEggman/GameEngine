// Editor navigation bake: a scene with a ground mesh + a zone -> collect the in-zone geometry in
// zone-local space -> Recast bake -> write the NavigationZoneAsset sidecar. The written blob loads
// back into a NavigationMesh and paths, proving the whole author-side chain end to end (headless).

#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.scene;
import foundation.geometry;
import engine.render;
import engine.navigation;
import foundation.navigation;
import foundation.navigation.resource;
import navigation.pipeline;
import editor.navigation;

using namespace foundation::core;
namespace scene = foundation::scene;
namespace geometry = foundation::geometry;
namespace content = foundation::content;
using namespace foundation::navigation;

namespace
{
    void RemoveTree(StringView root)
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
                }
            }
        }
        (void)RemoveDirectory(root);
    }

    RefPtr<geometry::StaticMesh> GroundMesh()
    {
        RefPtr<geometry::StaticMesh> mesh = MakeRef<geometry::StaticMesh>(DefaultAllocator());
        const Float3 corners[] = {{-10, 0, -10}, {10, 0, -10}, {10, 0, 10}, {-10, 0, 10}};
        for (const Float3& p : corners)
        {
            geometry::StaticMeshVertex v;
            v.position = p;
            v.normal = Float3{0, 1, 0};
            mesh->vertices.PushBack(v);
        }
        const u32 tris[] = {0, 3, 2, 0, 2, 1}; // +Y winding
        mesh->indices.Resize(sizeof(tris) / sizeof(tris[0])); // Add writes into a sized buffer
        for (u32 i : tris)
        {
            mesh->indices.Add(i);
        }
        mesh->CalculateBounds();
        return mesh;
    }
}

TEST_CASE("editor.navigation: bake collects scene geometry and writes a loadable zone asset")
{
    RegisterNavigationResource();
    pipeline::RegisterNavigationZoneAsset();
    RemoveTree(u8"scratch_navbake_db");

    foundation::vfs::NativeFileSystem mount(u8"scratch_navbake_db");
    content::ContentDatabase db(mount, BinarySerializerFactory(), u8".rasset");
    auto* assetInstance =
        db.RootGroup()->CreateInstance(u8"zone", pipeline::NavigationZoneAsset::StaticType());
    REQUIRE(assetInstance != nullptr);

    // Scene: a ground mesh at the origin + a zone entity that covers it.
    scene::Scene sceneObj(u8"bake");
    engine::navigation::AddNavigationSceneManagers(sceneObj);
    auto* meshes = sceneObj.AddSystem<engine::render::MeshComponentManager>();

    RefPtr<geometry::StaticMesh> ground = GroundMesh();
    scene::EntityHandle groundEntity = sceneObj.CreateEntity(u8"ground");
    engine::render::MeshComponent& mc = meshes->Add(groundEntity);
    mc.mesh = foundation::resource::Ref<geometry::StaticMesh>(ground);

    scene::EntityHandle zoneEntity = sceneObj.CreateEntity(u8"zone");
    engine::navigation::NavMeshZoneComponent& z =
        sceneObj.GetSystem<engine::navigation::NavMeshZoneComponentManager>()->Add(zoneEntity);
    z.extents = Float3{15, 10, 15};

    sceneObj.UpdateTransforms();

    // Bake: collect the ground triangles (2) and write the navmesh into the asset sidecar.
    const editor::navigation::BakeResult result =
        editor::navigation::BakeNavigationZone(sceneObj, zoneEntity, *assetInstance);
    CHECK(result.triangleCount == 2u);
    CHECK(result.baked);

    // The written asset carries a navmesh (in the sidecar) that loads and paths.
    pipeline::NavigationZoneAsset readBack;
    {
        RefPtr<ISerializable> object = assetInstance->ReadObject();
        auto* asset = Cast<pipeline::NavigationZoneAsset>(object.Get());
        REQUIRE(asset != nullptr);
        REQUIRE(pipeline::EnsureNavMeshLoaded(*assetInstance, *asset).IsOk());
        REQUIRE(!asset->navMeshBlob.IsEmpty());
        readBack.navMeshBlob = asset->navMeshBlob;
    }
    NavigationMesh mesh;
    REQUIRE(mesh.Load(Span<const byte>{reinterpret_cast<const byte*>(readBack.navMeshBlob.Data()),
                                       readBack.navMeshBlob.Size()})
                .IsOk());
    REQUIRE(mesh.IsValid());
    NavigationMeshQuery query(mesh);
    NavigationPath path;
    REQUIRE(query.FindPath(Float3{-8, 0, 0}, Float3{8, 0, 0}, path).IsOk());
    CHECK(path.complete);

    RemoveTree(u8"scratch_navbake_db");
}
