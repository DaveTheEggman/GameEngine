/// Draconic::ModelImporter:mesh_convert - Model IR mesh -> geometry *Source.
///
/// Converts a `model::ModelMesh` (raw interleaved vertex bytes described by a
/// VertexElement layout + index buffer + material parts) into a cooked
/// `geometry::StaticMeshSource` / `SkinnedMeshSource`. Vertices are read BY SEMANTIC
/// (position/normal/uv/color/tangent[/joints/weights]) so any loader layout works;
/// the glTF/FBX loaders emit the canonical 48B (static) / 72B (skinned) interleave.
/// The model loaders always produce indexed geometry (non-indexed primitives get
/// sequential indices generated at load), so there is no non-indexed path here.

module;
#include "Core/Prelude.h"

export module draconic.modelimporter:mesh_convert;

import draconic.core;
import draconic.model;
import draconic.geometry;
import draconic.geometry.resource;

using namespace draconic::core;
namespace model = draconic::model;
namespace geometry = draconic::geometry;

namespace draconic::modelimporter {

// ---- per-element readers (by semantic; default if the element is absent) ----

[[nodiscard]] const model::VertexElement* FindElement(Span<const model::VertexElement> elems, model::VertexSemantic sem) noexcept
{
    for (const model::VertexElement& e : elems) { if (e.semantic == sem) { return &e; } }
    return nullptr;
}

[[nodiscard]] Vector3 ReadVec3(const u8* vtx, const model::VertexElement* e, Vector3 dflt) noexcept
{
    if (e == nullptr) { return dflt; }
    Vector3 r; MemCopy(&r, vtx + e->offset, sizeof(Vector3)); return r;
}
[[nodiscard]] Vector2 ReadVec2(const u8* vtx, const model::VertexElement* e, Vector2 dflt) noexcept
{
    if (e == nullptr) { return dflt; }
    Vector2 r; MemCopy(&r, vtx + e->offset, sizeof(Vector2)); return r;
}
[[nodiscard]] Vector4 ReadVec4(const u8* vtx, const model::VertexElement* e, Vector4 dflt) noexcept
{
    if (e == nullptr) { return dflt; }
    Vector4 r; MemCopy(&r, vtx + e->offset, sizeof(Vector4)); return r;
}
[[nodiscard]] u32 ReadU32(const u8* vtx, const model::VertexElement* e, u32 dflt) noexcept
{
    if (e == nullptr) { return dflt; }
    u32 r; MemCopy(&r, vtx + e->offset, sizeof(u32)); return r;
}

// ---- index conversion (Model IR is u16 or u32; cooked sources are u32) ----

void CopyIndices(const model::ModelMesh& mesh, Array<u32>& out)
{
    const i32 count = mesh.indexCount();
    out.Clear();
    out.Reserve(static_cast<usize>(count));
    const u8* data = mesh.getIndexData();
    if (data == nullptr || count <= 0) { return; }
    if (mesh.use32BitIndices()) {
        const u32* src = reinterpret_cast<const u32*>(data);
        for (i32 i = 0; i < count; ++i) { out.PushBack(src[i]); }
    } else {
        const u16* src = reinterpret_cast<const u16*>(data);
        for (i32 i = 0; i < count; ++i) { out.PushBack(static_cast<u32>(src[i])); }
    }
}

// ---- parts -> submesh ranges ----

void CopyParts(const model::ModelMesh& mesh, geometry::StaticMeshSource& out)
{
    out.subStart.Clear(); out.subCount.Clear(); out.subMaterial.Clear(); out.subPrim.Clear();
    const Span<const model::ModelMeshPart> parts = mesh.parts();
    if (parts.Size() == 0) {
        // No explicit parts: one submesh covering the whole index buffer.
        out.subStart.PushBack(0);
        out.subCount.PushBack(mesh.indexCount());
        out.subMaterial.PushBack(-1);
        out.subPrim.PushBack(static_cast<u8>(geometry::PrimitiveType::Triangles));
        return;
    }
    for (const model::ModelMeshPart& p : parts) {
        out.subStart.PushBack(p.indexStart);
        out.subCount.PushBack(p.indexCount);
        out.subMaterial.PushBack(p.materialIndex);
        out.subPrim.PushBack(static_cast<u8>(geometry::PrimitiveType::Triangles));
    }
}

export {

// Fill a StaticMeshSource from a model mesh's static streams (pos/normal/uv/color/tangent).
void StaticMeshSourceFromModel(const model::ModelMesh& mesh, geometry::StaticMeshSource& out)
{
    out.name = String(mesh.name());

    const Span<const model::VertexElement> elems = mesh.vertexElements();
    const model::VertexElement* ePos = FindElement(elems, model::VertexSemantic::Position);
    const model::VertexElement* eNrm = FindElement(elems, model::VertexSemantic::Normal);
    const model::VertexElement* eUv  = FindElement(elems, model::VertexSemantic::TexCoord);
    const model::VertexElement* eCol = FindElement(elems, model::VertexSemantic::Color);
    const model::VertexElement* eTan = FindElement(elems, model::VertexSemantic::Tangent);

    const i32 count  = mesh.vertexCount();
    const i32 stride = mesh.vertexStride();
    const u8* base   = mesh.getVertexData();

    out.vertexBlob.Clear();
    out.vertexBlob.Resize(static_cast<usize>(count) * sizeof(geometry::StaticMeshVertex));
    auto* dst = reinterpret_cast<geometry::StaticMeshVertex*>(out.vertexBlob.Data());
    for (i32 i = 0; i < count; ++i) {
        const u8* v = base + static_cast<usize>(i) * static_cast<usize>(stride);
        geometry::StaticMeshVertex sv{};
        sv.position = ReadVec3(v, ePos, Vector3{ 0, 0, 0 });
        sv.normal   = ReadVec3(v, eNrm, Vector3{ 0, 1, 0 });
        sv.texCoord = ReadVec2(v, eUv,  Vector2{ 0, 0 });
        sv.color    = ReadU32 (v, eCol, 0xFFFFFFFFu);
        sv.tangent  = ReadVec3(v, eTan, Vector3{ 1, 0, 0 });
        dst[i] = sv;
    }

    CopyIndices(mesh, out.indexData);
    CopyParts(mesh, out);
}

// Fill a SkinnedMeshSource: the static streams above + the parallel skinning stream
// (joints u16x4 + weights) and the owning skeleton index.
void SkinnedMeshSourceFromModel(const model::ModelMesh& mesh, i32 skeletonIndex, geometry::SkinnedMeshSource& out)
{
    StaticMeshSourceFromModel(mesh, out);
    out.skeletonIndex = skeletonIndex;

    const Span<const model::VertexElement> elems = mesh.vertexElements();
    const model::VertexElement* eJnt = FindElement(elems, model::VertexSemantic::Joints);
    const model::VertexElement* eWt  = FindElement(elems, model::VertexSemantic::Weights);

    const i32 count  = mesh.vertexCount();
    const i32 stride = mesh.vertexStride();
    const u8* base   = mesh.getVertexData();

    out.skinningBlob.Clear();
    out.skinningBlob.Resize(static_cast<usize>(count) * sizeof(geometry::VertexSkinning));
    auto* dst = reinterpret_cast<geometry::VertexSkinning*>(out.skinningBlob.Data());
    for (i32 i = 0; i < count; ++i) {
        const u8* v = base + static_cast<usize>(i) * static_cast<usize>(stride);
        geometry::VertexSkinning vs{};
        if (eJnt != nullptr) { MemCopy(vs.joints, v + eJnt->offset, sizeof(vs.joints)); }
        vs.weights = ReadVec4(v, eWt, Vector4{ 1, 0, 0, 0 });
        dst[i] = vs;
    }
}

} // export

} // namespace draconic::modelimporter
