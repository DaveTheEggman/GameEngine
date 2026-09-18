// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// FBX/OBJ loader: vertex welding and OBJ material sidecars. Both cases came from the Beef
// port, which found them by porting rather than reading: the welder keyed on the hash
// alone (a collision merged two different vertices into a pulled seam), and OBJ files
// never had their .mtl read (every material arrived named but white).
#include <doctest/doctest.h>
#include "Core/Prelude.h"

#include <cstdio>
#include <cstring>

import foundation.core;
import foundation.model;
import foundation.model.fbx;

using namespace foundation::core;
using namespace foundation::model;

namespace
{
    void WriteText(const char* path, const char* text)
    {
        // The engine's own file write (std::fopen is a C4996 error under MSVC /WX).
        REQUIRE(WriteFile(StringView(reinterpret_cast<const utf8char*>(path)),
                          Span<const byte>(reinterpret_cast<const byte*>(text), std::strlen(text)))
                    .IsOk());
    }

    // A quad as two triangles sharing an edge: 4 distinct vertices, 6 indices.
    constexpr const char* kQuadObj = R"(mtllib welded.mtl
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
vn 0 0 1
usemtl red
f 1//1 2//1 3//1
f 1//1 3//1 4//1
)";
    constexpr const char* kRedMtl = "newmtl red\nKd 1 0 0\n";
}

TEST_CASE("fbx.weld: colliding hashes never merge different vertices")
{
    fbx::VertexWelder welder(4);
    Array<u8> bytes;
    const u8 a[4] = {1, 2, 3, 4};
    const u8 b[4] = {9, 9, 9, 9};
    const u8 c[4] = {1, 2, 3, 4}; // same content as a
    const size_t sameHash = 0x1234u;
    CHECK(welder.Weld(sameHash, a, bytes) == 0);
    CHECK(welder.Weld(sameHash, b, bytes) == 1); // collided, but the bytes differ
    CHECK(welder.Weld(sameHash, c, bytes) == 0); // identical content welds
    CHECK(welder.Weld(0x9999u, c, bytes) == 2);  // a different hash is a different bucket
    CHECK(welder.Count() == 3);
    REQUIRE(bytes.Size() == 12u);
    CHECK(std::memcmp(bytes.Data() + 4, b, 4) == 0);
}

TEST_CASE("fbx.obj: a shared edge welds to distinct vertices only")
{
    WriteText("scratch_fbx_welded.obj", kQuadObj);
    WriteText("welded.mtl", kRedMtl);
    Model model;
    fbx::FbxLoader loader;
    REQUIRE(loader.load(StringView(reinterpret_cast<const utf8char*>("scratch_fbx_welded.obj")),
                        model) == ModelLoadResult::Ok);
    REQUIRE(model.meshes().Size() == 1u);
    const ModelMesh* mesh = model.meshes()[0];
    CHECK(mesh->indexCount() == 6);
    CHECK(mesh->vertexCount() == 4);
    std::remove("scratch_fbx_welded.obj");
    std::remove("welded.mtl");
}

TEST_CASE("fbx.obj: the .mtl sidecar is read, so usemtl carries its colour")
{
    WriteText("scratch_fbx_material.obj", kQuadObj);
    WriteText("welded.mtl", kRedMtl);
    Model model;
    fbx::FbxLoader loader;
    REQUIRE(loader.load(StringView(reinterpret_cast<const utf8char*>("scratch_fbx_material.obj")),
                        model) == ModelLoadResult::Ok);
    REQUIRE(model.materials().Size() >= 1u);
    const ModelMaterial* red = nullptr;
    for (const ModelMaterial* m : model.materials())
    {
        if (m->name() == StringView{u8"red"})
        {
            red = m;
        }
    }
    REQUIRE(red != nullptr);
    CHECK(red->baseColorFactor.x == doctest::Approx(1.0f));
    CHECK(red->baseColorFactor.y == doctest::Approx(0.0f));
    CHECK(red->baseColorFactor.z == doctest::Approx(0.0f));
    std::remove("scratch_fbx_material.obj");
    std::remove("welded.mtl");
}

TEST_CASE("fbx.obj: a missing .mtl is not an error")
{
    WriteText("scratch_fbx_nomtl.obj", kQuadObj); // references welded.mtl, which is absent
    Model model;
    fbx::FbxLoader loader;
    CHECK(loader.load(StringView(reinterpret_cast<const utf8char*>("scratch_fbx_nomtl.obj")),
                      model) == ModelLoadResult::Ok);
    CHECK(model.meshes().Size() == 1u);
    std::remove("scratch_fbx_nomtl.obj");
}
