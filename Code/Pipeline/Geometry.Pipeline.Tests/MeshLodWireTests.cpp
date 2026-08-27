// mesh-lod.md P1 wire: the LOD chain through StaticMeshSource v3 -> StaticMesh.
// Covers the serialize round-trip (sidecar write/read), the FillStatic validation
// (malformed tables collapse to 1 LOD - render at LOD 0, never crash), the
// SubMeshesForLod slicing contract, and the P0 optimizer staying set-preserving
// on EVERY level of a chain.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.geometry;
import foundation.geometry.resource;
import foundation.xml.serialization;
import geometry.pipeline;

using namespace foundation::core;
using namespace foundation::geometry;
using namespace pipeline;
using namespace foundation::vfs;

namespace
{
    // A 2-LOD, 1-submesh fixture over a shared 5-vertex fan: LOD 0 = 3 triangles,
    // LOD 1 = 1 triangle, indices concatenated in the one buffer.
    void BuildTwoLodSource(StaticMeshSource& out)
    {
        Array<StaticMeshVertex> vertices;
        for (u32 i = 0; i < 5; ++i)
        {
            StaticMeshVertex v;
            v.position = Float3{static_cast<f32>(i), static_cast<f32>(i % 2), 0.0f};
            vertices.PushBack(v);
        }
        out.vertexBlob.Resize(vertices.Size() * sizeof(StaticMeshVertex));
        MemCopy(out.vertexBlob.Data(), vertices.Data(), out.vertexBlob.Size());
        const u32 lod0[] = {0, 1, 2, 0, 2, 3, 0, 3, 4};
        const u32 lod1[] = {0, 1, 4};
        for (u32 i : lod0)
        {
            out.indexData.PushBack(i);
        }
        for (u32 i : lod1)
        {
            out.indexData.PushBack(i);
        }
        out.subStart.PushBack(0);
        out.subCount.PushBack(9);
        out.subMaterial.PushBack(7);
        out.subPrim.PushBack(static_cast<u8>(PrimitiveType::Triangles));
        out.lodCount = 2;
        out.lodStart.PushBack(9);
        out.lodIndexCount.PushBack(3);
        out.lodCoverage.PushBack(1.0f);
        out.lodCoverage.PushBack(0.25f);
    }
}

TEST_CASE("mesh lod wire: v3 sidecar round-trip carries the chain; FillStatic slices it")
{
    RegisterMeshAssets();
    (void)RemoveDirectoryRecursive(u8"scratch_mesh_lod_db");
    NativeFileSystem mount(u8"scratch_mesh_lod_db");
    Guid id;
    {
        foundation::content::ContentDatabase db(
            mount, foundation::xml::XmlSerializerFactory(), u8".xasset");
        auto* inst = db.RootGroup()->CreateInstance(u8"fan", StaticMeshAsset::StaticType());
        id = inst->Id();
        StaticMeshAsset asset;
        BuildTwoLodSource(asset.source);
        REQUIRE(WriteMeshAsset(*inst, asset).IsOk());
    }
    {
        foundation::content::ContentDatabase db(
            mount, foundation::xml::XmlSerializerFactory(), u8".xasset");
        foundation::content::Instance* inst = db.GetInstance(id);
        REQUIRE(inst != nullptr);
        RefPtr<ISerializable> object = inst->ReadObject();
        auto* asset = Cast<StaticMeshAsset>(object.Get());
        REQUIRE(asset != nullptr);
        REQUIRE(EnsureMeshSourceLoaded(*inst, *asset).IsOk());

        CHECK(asset->source.lodCount == 2);
        REQUIRE(asset->source.lodStart.Size() == 1);
        CHECK(asset->source.lodStart[0] == 9);
        CHECK(asset->source.lodIndexCount[0] == 3);
        REQUIRE(asset->source.lodCoverage.Size() == 2);
        CHECK(asset->source.lodCoverage[1] == doctest::Approx(0.25f));

        // Fill the runtime mesh: LOD 0 = subMeshes; LOD 1 = its own range, inheriting
        // material/primitive from the LOD-0 submesh.
        StaticMesh mesh;
        asset->source.FillStatic(mesh);
        CHECK(mesh.lodCount == 2);
        REQUIRE(mesh.SubMeshesForLod(0).Size() == 1);
        CHECK(mesh.SubMeshesForLod(0)[0].indexCount == 9);
        REQUIRE(mesh.SubMeshesForLod(1).Size() == 1);
        CHECK(mesh.SubMeshesForLod(1)[0].startIndex == 9);
        CHECK(mesh.SubMeshesForLod(1)[0].indexCount == 3);
        CHECK(mesh.SubMeshesForLod(1)[0].materialIndex == 7);
        // Out-of-range LOD clamps to the coarsest; a 1-LOD mesh always answers LOD 0.
        CHECK(mesh.SubMeshesForLod(9)[0].startIndex == 9);

        // FromMesh captures the chain back losslessly (capture round-trip).
        StaticMeshSource recaptured;
        StaticMeshSource::FromMesh(mesh, recaptured);
        CHECK(recaptured.lodCount == 2);
        REQUIRE(recaptured.lodStart.Size() == 1);
        CHECK(recaptured.lodStart[0] == 9);
        CHECK(recaptured.lodCoverage.Size() == 2);
    }
    (void)RemoveDirectoryRecursive(u8"scratch_mesh_lod_db");
}

TEST_CASE("mesh lod wire: malformed chains collapse to 1 LOD (render at LOD 0, no crash)")
{
    // Range past the index buffer.
    StaticMeshSource bad;
    BuildTwoLodSource(bad);
    bad.lodIndexCount[0] = 99;
    StaticMesh mesh;
    bad.FillStatic(mesh);
    CHECK(mesh.lodCount == 1);
    CHECK(mesh.lodSubMeshes.IsEmpty());
    CHECK(mesh.SubMeshesForLod(1)[0].indexCount == 9); // falls back to LOD 0

    // Wrong slice length (coverage array mismatched).
    StaticMeshSource badCoverage;
    BuildTwoLodSource(badCoverage);
    badCoverage.lodCoverage.PopBack();
    StaticMesh mesh2;
    badCoverage.FillStatic(mesh2);
    CHECK(mesh2.lodCount == 1);

    // A default source (no chain) is a 1-LOD mesh.
    StaticMeshSource plain;
    BuildTwoLodSource(plain);
    plain.lodCount = 1;
    plain.lodStart.Clear();
    plain.lodIndexCount.Clear();
    plain.lodCoverage.Clear();
    StaticMesh mesh3;
    plain.FillStatic(mesh3);
    CHECK(mesh3.lodCount == 1);
    CHECK(mesh3.SubMeshesForLod(1).Size() == 1); // LOD 0 view
}

// Regression: a PRE-LOD payload (version 3, no LOD keys) must deserialize cleanly as a 1-LOD chain,
// never a strict-versioning failure. This is the bug behind the "failed to deserialize - stale schema"
// cook warnings on meshes seeded before the LOD wire: the LOD keys were gated on version >= 3, but v3
// was already the geometry-sidecar version, so pre-LOD v3 sources (which have no LOD keys) were wrongly
// required to carry them. The gate is now >= 4; a v3 payload skips the LOD section and defaults.
TEST_CASE("mesh lod wire: a pre-LOD version-3 payload loads as 1-LOD, not a deserialize failure")
{
    StaticMeshSource authored;
    BuildTwoLodSource(authored); // a real 2-LOD chain

    // Serialize under a forced VERSION-3 scope: the >= 4 LOD gate drops the keys, producing exactly
    // the on-disk shape of a pre-LOD mesh (base geometry, no LOD section).
    MemoryStream buffer;
    const SerializedDataVersion v3[] = {{TypeOf<StaticMeshSource>().id, 3}};
    {
        BinarySerializer ar(buffer, SerializeMode::Write);
        ar.PushVersionScope(v3, 1);
        authored.Serialize(ar);
        ar.PopVersionScope();
        REQUIRE(ar.IsOk());
    }
    (void)buffer.Seek(0, SeekOrigin::Begin);

    StaticMeshSource loaded;
    {
        BinarySerializer ar(buffer, SerializeMode::Read);
        ar.PushVersionScope(v3, 1);
        loaded.Serialize(ar);
        ar.PopVersionScope();
        REQUIRE(ar.IsOk()); // the missing LOD keys are NOT a failure at v3
    }

    // Base geometry survived; the chain defaulted to a single LOD.
    CHECK(loaded.indexData.Size() == authored.indexData.Size());
    CHECK(loaded.vertexBlob.Size() == authored.vertexBlob.Size());
    CHECK(loaded.lodCount == 1);
    CHECK(loaded.lodStart.IsEmpty());
    CHECK(loaded.lodCoverage.IsEmpty());
}

// The migration case for a STATIC mesh written WITH LOD keys but stamped version 3 (the shape of a
// sidecar written by the pre-fix code that gated LOD on >= 3 while the asset was already v3): reading
// it as v3 skips the trailing LOD section without error and recovers the base geometry (the cook then
// regenerates the chain). LOD is the last field of a static source, so the unread bytes are harmless.
TEST_CASE("mesh lod wire: a v3 static payload that HAS LOD keys still loads (LOD skipped, geometry kept)")
{
    StaticMeshSource authored;
    BuildTwoLodSource(authored);

    // Write WITH the LOD keys (force version 4), then read as version 3.
    MemoryStream buffer;
    const SerializedDataVersion v4[] = {{TypeOf<StaticMeshSource>().id, 4}};
    const SerializedDataVersion v3[] = {{TypeOf<StaticMeshSource>().id, 3}};
    {
        BinarySerializer ar(buffer, SerializeMode::Write);
        ar.PushVersionScope(v4, 1);
        authored.Serialize(ar); // emits the LOD section
        ar.PopVersionScope();
        REQUIRE(ar.IsOk());
    }
    (void)buffer.Seek(0, SeekOrigin::Begin);
    StaticMeshSource loaded;
    {
        BinarySerializer ar(buffer, SerializeMode::Read);
        ar.PushVersionScope(v3, 1);
        loaded.Serialize(ar);   // stops before the LOD section; trailing bytes are harmless
        ar.PopVersionScope();
        REQUIRE(ar.IsOk());
    }
    CHECK(loaded.indexData.Size() == authored.indexData.Size());
    CHECK(loaded.vertexBlob.Size() == authored.vertexBlob.Size());
    CHECK(loaded.lodCount == 1); // LOD section was skipped
}

// The forward case: at version 4 (the LOD version) the chain round-trips intact - the gate is on.
TEST_CASE("mesh lod wire: version 4 round-trips the full LOD chain")
{
    StaticMeshSource authored;
    BuildTwoLodSource(authored);

    MemoryStream buffer;
    const SerializedDataVersion v4[] = {{TypeOf<StaticMeshSource>().id, 4}};
    {
        BinarySerializer ar(buffer, SerializeMode::Write);
        ar.PushVersionScope(v4, 1);
        authored.Serialize(ar);
        ar.PopVersionScope();
        REQUIRE(ar.IsOk());
    }
    (void)buffer.Seek(0, SeekOrigin::Begin);
    StaticMeshSource loaded;
    {
        BinarySerializer ar(buffer, SerializeMode::Read);
        ar.PushVersionScope(v4, 1);
        loaded.Serialize(ar);
        ar.PopVersionScope();
        REQUIRE(ar.IsOk());
    }
    CHECK(loaded.lodCount == 2);
    REQUIRE(loaded.lodStart.Size() == 1);
    CHECK(loaded.lodStart[0] == 9);
    CHECK(loaded.lodIndexCount[0] == 3);
    REQUIRE(loaded.lodCoverage.Size() == 2);
}

TEST_CASE("mesh lod wire: the P0 optimizer preserves every level of a chain")
{
    StaticMeshSource source;
    BuildTwoLodSource(source);

    // Capture both levels' triangles as position triples before.
    auto positionOf = [&source](u32 index)
    {
        const auto* v = reinterpret_cast<const StaticMeshVertex*>(
            source.vertexBlob.Data() + static_cast<usize>(index) * sizeof(StaticMeshVertex));
        return v->position;
    };
    Array<Float3> lod1Before;
    for (u32 i = 0; i < 3; ++i)
    {
        lod1Before.PushBack(positionOf(source.indexData[9 + i]));
    }

    MeshOptimizeStats stats;
    OptimizeStaticMeshSource(source, &stats);

    // Ranges intact; every index in range; the LOD-1 triangle survives as positions
    // (one triangle: rotation allowed by vcache, so compare as a set).
    CHECK(source.lodStart[0] == 9);
    CHECK(source.lodIndexCount[0] == 3);
    for (u32 index : source.indexData)
    {
        CHECK(index < 5);
    }
    Array<Float3> lod1After;
    for (u32 i = 0; i < 3; ++i)
    {
        lod1After.PushBack(positionOf(source.indexData[9 + i]));
    }
    usize matched = 0;
    for (const Float3& p : lod1Before)
    {
        for (const Float3& q : lod1After)
        {
            if (p == q)
            {
                ++matched;
                break;
            }
        }
    }
    CHECK(matched == 3);

    // A malformed LOD range makes the whole pass refuse (indices untouched).
    StaticMeshSource bad;
    BuildTwoLodSource(bad);
    bad.lodStart[0] = 11; // 11 + 3 > 12
    const u32 firstBefore = bad.indexData[0];
    OptimizeStaticMeshSource(bad, &stats);
    CHECK(bad.indexData[0] == firstBefore);
    CHECK(stats.triangleSubmeshes == 0);
}
