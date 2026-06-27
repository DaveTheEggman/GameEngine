// The engine runtime mesh format: vertex/stream sizes, the index buffer, StaticMesh
// geometry ops, and the key design point -- SkinnedMesh IS-A StaticMesh, so its static
// stream is usable anywhere a StaticMesh is, with the skinning stream discoverable via
// the virtual hooks. Plus the procedural primitives.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import raptor.core;
import raptor.geometry;

using namespace raptor::core;
using namespace raptor::geometry;

TEST_CASE("stream layouts are the GPU-canonical sizes")
{
    CHECK(sizeof(StaticMeshVertex) == 48);
    CHECK(sizeof(VertexSkinning) == 24);
    CHECK(StaticMesh::VertexStride() == 48);
    CHECK(SkinnedMesh::SkinningStride() == 24);
}

TEST_CASE("index buffer: format, set/get, raw size")
{
    IndexBuffer ib(IndexBuffer::Format::U16);
    CHECK(ib.IndexSize() == 2);
    ib.Resize(3);
    ib.AddTriangle(0, 1, 2);
    CHECK(ib.Count() == 3);
    CHECK(ib.Get(0) == 0);
    CHECK(ib.Get(2) == 2);
    CHECK(ib.DataSize() == 6);
    CHECK(ib.RawData() != nullptr);

    IndexBuffer ib32(IndexBuffer::Format::U32);
    CHECK(ib32.IndexSize() == 4);
    ib32.Resize(2);
    ib32.Set(0, 70000);                 // exceeds u16 range -> needs 32-bit
    CHECK(ib32.Get(0) == 70000);
}

TEST_CASE("static mesh: generated normals + tangents + bounds on a quad")
{
    RefPtr<StaticMesh> mesh = Primitives::Quad(2.0f, 2.0f);
    REQUIRE(mesh);
    CHECK(mesh->VertexCount() == 4);
    CHECK(mesh->IndexCount() == 6);
    CHECK(mesh->subMeshes.Size() == 1);

    mesh->GenerateNormals();
    for (const StaticMeshVertex& v : mesh->vertices) {
        CHECK(v.normal.z == doctest::Approx(1.0f));      // quad faces +Z
        CHECK(LengthSquared(v.tangent) == doctest::Approx(1.0f));   // unit tangents
    }

    mesh->CalculateBounds();
    CHECK(mesh->bounds.min.x == doctest::Approx(-1.0f));
    CHECK(mesh->bounds.max.y == doctest::Approx(1.0f));
    CHECK(mesh->VertexDataSize() == 4 * 48);
    CHECK(mesh->VertexData() != nullptr);
}

TEST_CASE("skinned mesh IS-A static mesh: static stream is substitutable")
{
    RefPtr<SkinnedMesh> skinned = MakeRef<SkinnedMesh>(DefaultAllocator());
    skinned->skeletonIndex = 3;
    // static stream (inherited)
    skinned->vertices.PushBack(StaticMeshVertex{ Vec3{ 0, 0, 0 }, Vec3{ 0, 1, 0 }, Vec2{ 0, 0 }, 0xFFFFFFFFu, Vec3{ 1, 0, 0 } });
    skinned->vertices.PushBack(StaticMeshVertex{ Vec3{ 1, 0, 0 }, Vec3{ 0, 1, 0 }, Vec2{ 1, 0 }, 0xFFFFFFFFu, Vec3{ 1, 0, 0 } });
    skinned->vertices.PushBack(StaticMeshVertex{ Vec3{ 0, 1, 0 }, Vec3{ 0, 1, 0 }, Vec2{ 0, 1 }, 0xFFFFFFFFu, Vec3{ 1, 0, 0 } });
    // parallel skinning stream
    VertexSkinning s{}; s.joints[0] = 2; s.weights = Vec4{ 1, 0, 0, 0 };
    for (u32 i = 0; i < 3; ++i) { skinned->skinning.PushBack(s); }

    // pass it where a StaticMesh& is expected -- the static ops just work
    StaticMesh& asStatic = *skinned;
    asStatic.CalculateBounds();
    CHECK(asStatic.VertexCount() == 3);
    CHECK(asStatic.bounds.max.x == doctest::Approx(1.0f));

    // a consumer holding the base ref can discover + reach the skinning stream
    CHECK(asStatic.IsSkinned());
    CHECK(asStatic.SkinningStream().Size() == 3);
    CHECK(asStatic.SkinningStream()[0].joints[0] == 2);

    // and downcast safely (reflection-based Cast, no RTTI)
    SkinnedMesh* down = Cast<SkinnedMesh>(&asStatic);
    REQUIRE(down != nullptr);
    CHECK(down->skeletonIndex == 3);
    CHECK(down->SkinningDataSize() == 3 * 24);

    // a plain static mesh reports not-skinned + an empty stream
    StaticMesh plain;
    CHECK_FALSE(plain.IsSkinned());
    CHECK(plain.SkinningStream().Size() == 0);
    CHECK(Cast<SkinnedMesh>(&plain) == nullptr);
}

TEST_CASE("primitives: cube + sphere + plane are well-formed")
{
    RefPtr<StaticMesh> cube = Primitives::Cube(2.0f);
    REQUIRE(cube);
    CHECK(cube->VertexCount() == 24);             // 4 verts x 6 faces (hard normals)
    CHECK(cube->IndexCount() == 36);
    CHECK(cube->bounds.min.x == doctest::Approx(-1.0f));
    CHECK(cube->bounds.max.z == doctest::Approx(1.0f));

    RefPtr<StaticMesh> sphere = Primitives::Sphere(1.0f, 16, 8);
    REQUIRE(sphere);
    CHECK(sphere->IndexCount() == 16 * 8 * 6);
    // every surface point is ~radius from the origin
    for (const StaticMeshVertex& v : sphere->vertices) {
        CHECK(Length(v.position) == doctest::Approx(1.0f).epsilon(0.01));
    }

    RefPtr<StaticMesh> plane = Primitives::Plane(4.0f, 4.0f, 2, 2);
    REQUIRE(plane);
    CHECK(plane->VertexCount() == 9);             // (2+1) x (2+1)
    CHECK(plane->IndexCount() == 2 * 2 * 6);
}

TEST_CASE("clear-for-reload empties in place (skinned clears both streams)")
{
    RefPtr<SkinnedMesh> mesh = MakeRef<SkinnedMesh>(DefaultAllocator());
    mesh->vertices.PushBack(StaticMeshVertex{});
    mesh->skinning.PushBack(VertexSkinning{});
    mesh->skeletonIndex = 5;

    mesh->ClearForReload();
    CHECK(mesh->VertexCount() == 0);
    CHECK(mesh->SkinningStream().Size() == 0);
    CHECK(mesh->skeletonIndex == -1);
}
