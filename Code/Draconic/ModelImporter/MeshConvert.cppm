/// Draconic::ModelImporter:mesh_convert — Model IR mesh -> geometry *Source.
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
namespace mdl = draconic::model;
namespace geo = draconic::geometry;

namespace draconic::modelimporter {

// ---- per-element readers (by semantic; default if the element is absent) ----

[[nodiscard]] const mdl::VertexElement* FindElement(Span<const mdl::VertexElement> elems, mdl::VertexSemantic sem) noexcept
{
    for (const mdl::VertexElement& e : elems) { if (e.semantic == sem) { return &e; } }
    return nullptr;
}

[[nodiscard]] Vec3 ReadVec3(const u8* vtx, const mdl::VertexElement* e, Vec3 dflt) noexcept
{
    if (e == nullptr) { return dflt; }
    Vec3 r; MemCopy(&r, vtx + e->offset, sizeof(Vec3)); return r;
}
[[nodiscard]] Vec2 ReadVec2(const u8* vtx, const mdl::VertexElement* e, Vec2 dflt) noexcept
{
    if (e == nullptr) { return dflt; }
    Vec2 r; MemCopy(&r, vtx + e->offset, sizeof(Vec2)); return r;
}
[[nodiscard]] Vec4 ReadVec4(const u8* vtx, const mdl::VertexElement* e, Vec4 dflt) noexcept
{
    if (e == nullptr) { return dflt; }
    Vec4 r; MemCopy(&r, vtx + e->offset, sizeof(Vec4)); return r;
}
[[nodiscard]] u32 ReadU32(const u8* vtx, const mdl::VertexElement* e, u32 dflt) noexcept
{
    if (e == nullptr) { return dflt; }
    u32 r; MemCopy(&r, vtx + e->offset, sizeof(u32)); return r;
}

// ---- index conversion (Model IR is u16 or u32; cooked sources are u32) ----

void CopyIndices(const mdl::ModelMesh& mesh, Array<u32>& out)
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

void CopyParts(const mdl::ModelMesh& mesh, geo::StaticMeshSource& out)
{
    out.subStart.Clear(); out.subCount.Clear(); out.subMaterial.Clear(); out.subPrim.Clear();
    const Span<const mdl::ModelMeshPart> parts = mesh.parts();
    if (parts.Size() == 0) {
        // No explicit parts: one submesh covering the whole index buffer.
        out.subStart.PushBack(0);
        out.subCount.PushBack(mesh.indexCount());
        out.subMaterial.PushBack(-1);
        out.subPrim.PushBack(static_cast<u8>(geo::PrimitiveType::Triangles));
        return;
    }
    for (const mdl::ModelMeshPart& p : parts) {
        out.subStart.PushBack(p.indexStart);
        out.subCount.PushBack(p.indexCount);
        out.subMaterial.PushBack(p.materialIndex);
        out.subPrim.PushBack(static_cast<u8>(geo::PrimitiveType::Triangles));
    }
}

export {

// Fill a StaticMeshSource from a model mesh's static streams (pos/normal/uv/color/tangent).
void StaticMeshSourceFromModel(const mdl::ModelMesh& mesh, geo::StaticMeshSource& out)
{
    out.name = String(mesh.name());

    const Span<const mdl::VertexElement> elems = mesh.vertexElements();
    const mdl::VertexElement* ePos = FindElement(elems, mdl::VertexSemantic::Position);
    const mdl::VertexElement* eNrm = FindElement(elems, mdl::VertexSemantic::Normal);
    const mdl::VertexElement* eUv  = FindElement(elems, mdl::VertexSemantic::TexCoord);
    const mdl::VertexElement* eCol = FindElement(elems, mdl::VertexSemantic::Color);
    const mdl::VertexElement* eTan = FindElement(elems, mdl::VertexSemantic::Tangent);

    const i32 count  = mesh.vertexCount();
    const i32 stride = mesh.vertexStride();
    const u8* base   = mesh.getVertexData();

    out.vertexBlob.Clear();
    out.vertexBlob.Resize(static_cast<usize>(count) * sizeof(geo::StaticMeshVertex));
    auto* dst = reinterpret_cast<geo::StaticMeshVertex*>(out.vertexBlob.Data());
    for (i32 i = 0; i < count; ++i) {
        const u8* v = base + static_cast<usize>(i) * static_cast<usize>(stride);
        geo::StaticMeshVertex sv{};
        sv.position = ReadVec3(v, ePos, Vec3{ 0, 0, 0 });
        sv.normal   = ReadVec3(v, eNrm, Vec3{ 0, 1, 0 });
        sv.texCoord = ReadVec2(v, eUv,  Vec2{ 0, 0 });
        sv.color    = ReadU32 (v, eCol, 0xFFFFFFFFu);
        sv.tangent  = ReadVec3(v, eTan, Vec3{ 1, 0, 0 });
        dst[i] = sv;
    }

    CopyIndices(mesh, out.indexData);
    CopyParts(mesh, out);
}

// Fill a SkinnedMeshSource: the static streams above + the parallel skinning stream
// (joints u16x4 + weights) and the owning skeleton index.
void SkinnedMeshSourceFromModel(const mdl::ModelMesh& mesh, i32 skeletonIndex, geo::SkinnedMeshSource& out)
{
    StaticMeshSourceFromModel(mesh, out);
    out.skeletonIndex = skeletonIndex;

    const Span<const mdl::VertexElement> elems = mesh.vertexElements();
    const mdl::VertexElement* eJnt = FindElement(elems, mdl::VertexSemantic::Joints);
    const mdl::VertexElement* eWt  = FindElement(elems, mdl::VertexSemantic::Weights);

    const i32 count  = mesh.vertexCount();
    const i32 stride = mesh.vertexStride();
    const u8* base   = mesh.getVertexData();

    out.skinningBlob.Clear();
    out.skinningBlob.Resize(static_cast<usize>(count) * sizeof(geo::VertexSkinning));
    auto* dst = reinterpret_cast<geo::VertexSkinning*>(out.skinningBlob.Data());
    for (i32 i = 0; i < count; ++i) {
        const u8* v = base + static_cast<usize>(i) * static_cast<usize>(stride);
        geo::VertexSkinning vs{};
        if (eJnt != nullptr) { MemCopy(vs.joints, v + eJnt->offset, sizeof(vs.joints)); }
        vs.weights = ReadVec4(v, eWt, Vec4{ 1, 0, 0, 0 });
        dst[i] = vs;
    }
}

} // export

} // namespace draconic::modelimporter
