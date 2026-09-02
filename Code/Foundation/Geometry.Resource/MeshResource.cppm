// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Foundation::Geometry.Resource - the `foundation.geometry.resource` module.
///
/// Meshes as resources: a StaticMeshSource / SkinnedMeshSource (cooked content - the
/// raw vertex stream, indices, submeshes, and for skinned the parallel skinning
/// stream + skeleton ref) is built by a factory into the runtime StaticMesh /
/// SkinnedMesh. Two source/factory pairs, mirroring the mesh hierarchy: SkinnedMeshSource
/// IS-A StaticMeshSource, so the static serialization + fill logic is shared (the skinned
/// side only adds the skinning stream). Bounds are recomputed on build (derivable from
/// the vertices), so they aren't stored.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.geometry.resource;

import foundation.core;
import foundation.resource;
import foundation.content;
import foundation.geometry;

using namespace foundation::core;
using namespace foundation::resource;

export namespace foundation::geometry
{

    // Cooked static mesh: raw static-stream bytes + 32-bit indices + submeshes (parallel
    // arrays, so they serialize via the primitive Array<T> path).
    class StaticMeshSource : public ISerializable
    {
        RTTI_OBJECT(StaticMeshSource, ISerializable)
    public:
        String name;
        Array<u8> vertexBlob; // raw StaticMeshVertex bytes (48B each)
        Array<u32> indexData; // 32-bit indices
        Array<i32> subStart;
        Array<i32> subCount;
        Array<i32> subMaterial;
        Array<u8> subPrim; // PrimitiveType

        // LOD chain (v3): levels 1..lodCount-1 as flattened per-submesh
        // index ranges ((lod-1) * submeshCount + submesh) into the SAME indexData, over
        // the SAME vertexBlob; lodCoverage carries lodCount switch thresholds. Legacy
        // (v<3) payloads load as 1-LOD chains (fields default). Materials/primitive
        // types per level mirror the LOD-0 submesh (a coarser level never resorts).
        u32 lodCount = 1;
        Array<i32> lodStart;
        Array<i32> lodIndexCount;
        Array<f32> lodCoverage;

        void Serialize(ISerializer& ar) override { SerializeStatic(ar); }

        // Captures a StaticMesh into this source (for cooking).
        static void FromMesh(const StaticMesh& mesh, StaticMeshSource& out)
        {
            out.name = String(mesh.name.AsView());
            out.vertexBlob.Clear();
            out.vertexBlob.Resize(mesh.VertexDataSize());
            if (mesh.VertexDataSize() > 0)
            {
                MemCopy(out.vertexBlob.Data(), mesh.VertexData(), mesh.VertexDataSize());
            }
            out.indexData.Clear();
            out.indexData.Reserve(mesh.IndexCount());
            for (u32 i = 0; i < mesh.IndexCount(); ++i)
            {
                out.indexData.PushBack(mesh.indices.Get(i));
            }
            out.subStart.Clear();
            out.subCount.Clear();
            out.subMaterial.Clear();
            out.subPrim.Clear();
            for (const SubMesh& sm : mesh.subMeshes)
            {
                out.subStart.PushBack(sm.startIndex);
                out.subCount.PushBack(sm.indexCount);
                out.subMaterial.PushBack(sm.materialIndex);
                out.subPrim.PushBack(static_cast<u8>(sm.primitiveType));
            }
            out.lodCount = (mesh.lodCount > 0) ? mesh.lodCount : 1;
            out.lodStart.Clear();
            out.lodIndexCount.Clear();
            for (const SubMesh& sm : mesh.lodSubMeshes)
            {
                out.lodStart.PushBack(sm.startIndex);
                out.lodIndexCount.PushBack(sm.indexCount);
            }
            out.lodCoverage = mesh.lodCoverage;
        }

        // Populates a StaticMesh from this source (recomputes bounds).
        void FillStatic(StaticMesh& mesh) const
        {
            mesh.name = String(name.AsView());
            const u32 vcount = static_cast<u32>(vertexBlob.Size() / sizeof(StaticMeshVertex));
            mesh.vertices.Clear();
            mesh.vertices.Resize(vcount);
            if (vcount > 0)
            {
                MemCopy(mesh.vertices.Data(), vertexBlob.Data(), vcount * sizeof(StaticMeshVertex));
            }
            mesh.indices.Resize(static_cast<u32>(indexData.Size()));
            for (usize i = 0; i < indexData.Size(); ++i)
            {
                mesh.indices.Set(static_cast<u32>(i), indexData[i]);
            }
            mesh.subMeshes.Clear();
            for (usize i = 0; i < subStart.Size(); ++i)
            {
                mesh.subMeshes.PushBack(
                    SubMesh{subStart[i], subCount[i], (i < subMaterial.Size() ? subMaterial[i] : 0),
                            static_cast<PrimitiveType>(i < subPrim.Size() ? subPrim[i] : 0)});
            }
            // The LOD chain, validated: a malformed table (wrong slice length, a range
            // outside the index buffer) collapses to 1 LOD - bad data renders at LOD 0
            // rather than crashing selection. Levels mirror LOD 0's material/primitive.
            mesh.lodCount = 1;
            mesh.lodSubMeshes.Clear();
            mesh.lodCoverage.Clear();
            const usize per = subStart.Size();
            if (lodCount > 1 && per > 0 &&
                lodStart.Size() == static_cast<usize>(lodCount - 1) * per &&
                lodIndexCount.Size() == lodStart.Size() && lodCoverage.Size() == lodCount)
            {
                bool valid = true;
                for (usize i = 0; i < lodStart.Size() && valid; ++i)
                {
                    const i64 start = lodStart[i];
                    const i64 count = lodIndexCount[i];
                    valid = start >= 0 && count >= 0 &&
                            start + count <= static_cast<i64>(indexData.Size());
                }
                if (valid)
                {
                    mesh.lodCount = lodCount;
                    mesh.lodCoverage = lodCoverage;
                    for (usize i = 0; i < lodStart.Size(); ++i)
                    {
                        const usize submesh = i % per;
                        mesh.lodSubMeshes.PushBack(SubMesh{
                            lodStart[i], lodIndexCount[i], mesh.subMeshes[submesh].materialIndex,
                            mesh.subMeshes[submesh].primitiveType});
                    }
                }
            }
            mesh.CalculateBounds();
        }

    protected:
        // Shared by the skinned subclass so it can append after the static fields.
        void SerializeStatic(ISerializer& ar)
        {
            foundation::core::Serialize(ar, "name", name);
            foundation::core::Serialize(ar, "vertexBlob", vertexBlob);
            // v1 blobs predate the Float4 tangent (48-byte stride, Float3 tangent at offset 36):
            // expand each vertex in place with handedness +1 - identical look, no re-authoring.
            if (ar.Mode() == SerializeMode::Read && ar.Version() < 2)
            {
                constexpr usize kOldStride = 48;
                if (vertexBlob.Size() % kOldStride == 0 && !vertexBlob.IsEmpty())
                {
                    const usize count = vertexBlob.Size() / kOldStride;
                    Array<u8> wide;
                    wide.Resize(count * sizeof(StaticMeshVertex));
                    for (usize i = 0; i < count; ++i)
                    {
                        const u8* src = vertexBlob.Data() + i * kOldStride;
                        auto* dst = reinterpret_cast<StaticMeshVertex*>(
                            wide.Data() + i * sizeof(StaticMeshVertex));
                        *dst = StaticMeshVertex{};
                        MemCopy(dst, src, 36); // pos/normal/uv/color
                        Float3 t3{1, 0, 0};
                        MemCopy(&t3, src + 36, sizeof(t3)); // old Float3 tangent
                        dst->tangent = Float4{t3.x, t3.y, t3.z, 1.0f};
                    }
                    vertexBlob = Move(wide);
                }
            }
            foundation::core::Serialize(ar, "indexData", indexData);
            foundation::core::Serialize(ar, "subStart", subStart);
            foundation::core::Serialize(ar, "subCount", subCount);
            foundation::core::Serialize(ar, "subMaterial", subMaterial);
            foundation::core::Serialize(ar, "subPrim", subPrim);
            // v4: the LOD chain. Older payloads (v3 = pre-LOD, and below) stay 1-LOD
            // via the field defaults - strict versioning uses the gate, NOT optional keys. NOTE the
            // gate is >= 4, not >= 3: v3 was already the geometry-sidecar version, so gating LOD on
            // >= 3 wrongly required LOD keys from pre-LOD v3 sources and failed their deserialization.
            if (ar.Version() >= 4)
            {
                foundation::core::Serialize(ar, "lodCount", lodCount);
                foundation::core::Serialize(ar, "lodStart", lodStart);
                foundation::core::Serialize(ar, "lodIndexCount", lodIndexCount);
                foundation::core::Serialize(ar, "lodCoverage", lodCoverage);
            }
        }
    };

    // Cooked skinned mesh: the static cooked data + the parallel skinning stream + skeleton.
    class SkinnedMeshSource final : public StaticMeshSource
    {
        RTTI_OBJECT(SkinnedMeshSource, StaticMeshSource)
    public:
        Array<u8> skinningBlob; // raw VertexSkinning bytes (24B each)
        i32 skeletonIndex = -1;

        void Serialize(ISerializer& ar) override
        {
            SerializeStatic(ar);
            foundation::core::Serialize(ar, "skinningBlob", skinningBlob);
            foundation::core::Serialize(ar, "skeletonIndex", skeletonIndex);
            // v3 poison check: the buggy >= 3 LOD gate (2026-08-23..27) wrote LOD keys into
            // v3-stamped payloads. Binary reads are positional, so on a skinned v3 payload the
            // skinning fields above would consume the LOD bytes and yield a garbage skin stream
            // with ar.IsOk() still true. The parallel-stream invariant (one 24B VertexSkinning per
            // vertex) catches exactly that misalignment - fail LOUDLY so the asset re-imports
            // instead of animating garbage.
            if (ar.Mode() == SerializeMode::Read && ar.IsPayloadOk())
            {
                const usize vcount = vertexBlob.Size() / sizeof(StaticMeshVertex);
                if (skinningBlob.Size() != vcount * sizeof(VertexSkinning))
                {
                    ar.FailPayload(ErrorCode::InvalidArgument);
                }
            }
        }

        static void FromMesh(const SkinnedMesh& mesh, SkinnedMeshSource& out)
        {
            StaticMeshSource::FromMesh(mesh, out); // static fields
            out.skinningBlob.Clear();
            out.skinningBlob.Resize(mesh.SkinningDataSize());
            if (mesh.SkinningDataSize() > 0)
            {
                MemCopy(out.skinningBlob.Data(), mesh.SkinningData(), mesh.SkinningDataSize());
            }
            out.skeletonIndex = mesh.skeletonIndex;
        }

        void FillSkinned(SkinnedMesh& mesh) const
        {
            FillStatic(mesh); // base static stream + indices + submeshes + bounds
            const u32 scount = static_cast<u32>(skinningBlob.Size() / sizeof(VertexSkinning));
            mesh.skinning.Clear();
            mesh.skinning.Resize(scount);
            if (scount > 0)
            {
                MemCopy(mesh.skinning.Data(), skinningBlob.Data(), scount * sizeof(VertexSkinning));
            }
            mesh.skeletonIndex = skeletonIndex;
        }
    };

    // Builds a StaticMeshSource into a runtime StaticMesh.
    //
    // The runtime StaticMesh/SkinnedMesh is a PURE-CPU product (vertex/index arrays + submeshes;
    // the renderer uploads to GPU buffers separately), so the async path (task #123) does the
    // whole build - including the heavy vertex/index blob copy - on a JobSystem worker and finalize
    // is a no-op. Safe: content-DB reads open independent streams and the mesh source + product
    // types are registered on the main thread at startup (RegisterModelResource / AddFactory).
    class StaticMeshFactory final : public IResourceFactory
    {
    public:
        // The allocator backs every product this factory creates (required -
        // the application that registers the factory decides).
        explicit StaticMeshFactory(IAllocator& allocator) noexcept : m_allocator(&allocator) {}

        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &StaticMesh::StaticType();
        }
        [[nodiscard]] RefPtr<Object> Create(ResourceManager&,
                                            foundation::content::Instance& instance) override
        {
            return BuildMesh(instance);
        }
        [[nodiscard]] bool SupportsAsync() const override { return true; }
        [[nodiscard]] RefPtr<Object> DecodeStage(foundation::content::Instance& instance) override
        {
            return BuildMesh(instance);
        }
        [[nodiscard]] RefPtr<Object> FinalizeStage(ResourceManager&, RefPtr<Object> decoded) override
        {
            return decoded;
        }

    private:
        [[nodiscard]] RefPtr<Object> BuildMesh(foundation::content::Instance& instance) const
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            // A SkinnedMeshSource IS-A StaticMeshSource, so a Ref<StaticMesh> can legitimately bind
            // a skinned product (the picker offers both). Build the REAL SkinnedMesh then - FillStatic
            // would silently drop the skin stream and the mesh could never animate.
            if (SkinnedMeshSource* skinned = Cast<SkinnedMeshSource>(object.Get()))
            {
                RefPtr<SkinnedMesh> mesh = MakeRef<SkinnedMesh>((*m_allocator));
                skinned->FillSkinned(*mesh);
                return mesh;
            }
            StaticMeshSource* src = Cast<StaticMeshSource>(object.Get());
            if (src == nullptr)
            {
                return RefPtr<Object>{};
            }
            RefPtr<StaticMesh> mesh = MakeRef<StaticMesh>((*m_allocator));
            src->FillStatic(*mesh);
            return mesh;
        }
    
    private:
        IAllocator* m_allocator;
    };

    // Builds a SkinnedMeshSource into a runtime SkinnedMesh. Pure-CPU (see StaticMeshFactory).
    class SkinnedMeshFactory final : public IResourceFactory
    {
    public:
        // The allocator backs every product this factory creates (required -
        // the application that registers the factory decides).
        explicit SkinnedMeshFactory(IAllocator& allocator) noexcept : m_allocator(&allocator) {}

        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &SkinnedMesh::StaticType();
        }
        [[nodiscard]] RefPtr<Object> Create(ResourceManager&,
                                            foundation::content::Instance& instance) override
        {
            return BuildMesh(instance);
        }
        [[nodiscard]] bool SupportsAsync() const override { return true; }
        [[nodiscard]] RefPtr<Object> DecodeStage(foundation::content::Instance& instance) override
        {
            return BuildMesh(instance);
        }
        [[nodiscard]] RefPtr<Object> FinalizeStage(ResourceManager&, RefPtr<Object> decoded) override
        {
            return decoded;
        }

    private:
        [[nodiscard]] RefPtr<Object> BuildMesh(foundation::content::Instance& instance) const
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            SkinnedMeshSource* src = Cast<SkinnedMeshSource>(object.Get());
            if (src == nullptr)
            {
                return RefPtr<Object>{};
            }
            RefPtr<SkinnedMesh> mesh = MakeRef<SkinnedMesh>((*m_allocator));
            src->FillSkinned(*mesh);
            return mesh;
        }
    
    private:
        IAllocator* m_allocator;
    };

    // v4 = LOD chain gated on version >= 4 (SerializeStatic). This is the authority for the COOKED
    // stream (WriteObject(source)); the source .xasset side keys off the mesh-ASSET version, which is
    // also bumped to 4 in lockstep, so the >= 4 gate means the same thing in both paths. (The LOD wire
    // reused v3, which the geometry sidecar already occupied - see MeshAsset.cppm.) v3 = the source
    // pre-LOD; v<2 = Float3 tangent (migrated in SerializeStatic).
    RTTI_DEFINE_OBJECT_VERSIONED(StaticMeshSource, "rtti::geometry", 4)
    RTTI_DEFINE_OBJECT_VERSIONED(SkinnedMeshSource, "rtti::geometry", 4)

} // namespace foundation::geometry
