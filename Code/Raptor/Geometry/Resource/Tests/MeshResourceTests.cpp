// Meshes as resources: cook a mesh (capture -> source -> content DB), then build it
// back through the ResourceManager via the factory and verify the runtime mesh round-
// trips its vertices/indices/submeshes/bounds. Covers both the static and skinned paths.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import raptor.core;
import raptor.vfs;
import raptor.content;
import raptor.resource;
import raptor.geometry;
import raptor.geometry.resource;

using namespace raptor::core;
using namespace raptor::vfs;
using namespace raptor::resource;
using namespace raptor::geometry;

namespace
{
    void RemoveTree()
    {
        FileDelete(u8"raptor_mesh_res_db/cube.rasset");
        FileDelete(u8"raptor_mesh_res_db/skinned.rasset");
        RemoveDirectory(u8"raptor_mesh_res_db");
    }
}

TEST_CASE("static mesh resource: cube round-trips through the resource manager")
{
    GlobalTypeRegistry().Register(StaticMeshSource::StaticType());
    RegisterSerializable<StaticMeshSource>();
    GlobalTypeRegistry().Register(StaticMesh::StaticType());

    RemoveTree();
    NativeFileSystem mount(u8"raptor_mesh_res_db");

    Guid id;
    {
        raptor::content::ContentDatabase db(mount);
        auto* inst = db.RootGroup()->CreateInstance(u8"cube", StaticMeshSource::StaticType());
        id = inst->Id();

        RefPtr<StaticMesh> cube = Primitives::Cube(2.0f);
        StaticMeshSource src;
        StaticMeshSource::FromMesh(*cube, src);
        REQUIRE(inst->WriteObject(src).IsOk());
    }

    raptor::content::ContentDatabase db(mount);
    StaticMeshFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    Proxy<StaticMesh> cube = manager.Bind<StaticMesh>(id);
    REQUIRE(cube);
    CHECK(cube->VertexCount() == 24);
    CHECK(cube->IndexCount() == 36);
    CHECK(cube->subMeshes.Size() == 1);
    CHECK_FALSE(cube->IsSkinned());
    CHECK(cube->bounds.min.x == doctest::Approx(-1.0f));
    CHECK(cube->bounds.max.z == doctest::Approx(1.0f));

    RemoveTree();
}

TEST_CASE("skinned mesh resource: round-trips the static + skinning streams")
{
    GlobalTypeRegistry().Register(StaticMeshSource::StaticType());
    GlobalTypeRegistry().Register(SkinnedMeshSource::StaticType());
    RegisterSerializable<SkinnedMeshSource>();
    GlobalTypeRegistry().Register(StaticMesh::StaticType());
    GlobalTypeRegistry().Register(SkinnedMesh::StaticType());

    RemoveTree();
    NativeFileSystem mount(u8"raptor_mesh_res_db");

    Guid id;
    {
        raptor::content::ContentDatabase db(mount);
        auto* inst = db.RootGroup()->CreateInstance(u8"skinned", SkinnedMeshSource::StaticType());
        id = inst->Id();

        RefPtr<SkinnedMesh> mesh = MakeRef<SkinnedMesh>(DefaultAllocator());
        mesh->skeletonIndex = 7;
        for (u32 i = 0; i < 3; ++i) {
            mesh->vertices.PushBack(StaticMeshVertex{ Vec3{ static_cast<f32>(i), 0, 0 }, Vec3{ 0, 1, 0 }, Vec2{ 0, 0 }, 0xFFFFFFFFu, Vec3{ 1, 0, 0 } });
            VertexSkinning s{}; s.joints[0] = static_cast<u16>(i); s.weights = Vec4{ 1, 0, 0, 0 };
            mesh->skinning.PushBack(s);
        }
        mesh->indices.Resize(3);
        mesh->indices.AddTriangle(0, 1, 2);

        SkinnedMeshSource src;
        SkinnedMeshSource::FromMesh(*mesh, src);
        REQUIRE(inst->WriteObject(src).IsOk());
    }

    raptor::content::ContentDatabase db(mount);
    SkinnedMeshFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    Proxy<SkinnedMesh> mesh = manager.Bind<SkinnedMesh>(id);
    REQUIRE(mesh);
    CHECK(mesh->VertexCount() == 3);
    CHECK(mesh->IndexCount() == 3);
    CHECK(mesh->IsSkinned());
    CHECK(mesh->skeletonIndex == 7);
    REQUIRE(mesh->SkinningStream().Size() == 3);
    CHECK(mesh->SkinningStream()[2].joints[0] == 2);

    // the skinned product is also usable as a StaticMesh
    Proxy<StaticMesh> asStatic = manager.Bind<StaticMesh>(id);
    REQUIRE(asStatic);
    CHECK(asStatic->VertexCount() == 3);
    CHECK(asStatic->IsSkinned());

    RemoveTree();
}
