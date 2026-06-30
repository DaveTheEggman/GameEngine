/// Draconic::Geometry — the `:mesh` partition.
///
/// StaticMesh is the engine's runtime mesh: a static vertex stream (48B), an index
/// buffer, submeshes, bounds, and the geometry ops (normals/tangents/bounds) that
/// operate purely on the static stream.
///
/// SkinnedMesh IS-A StaticMesh: it inherits the entire static stream + all the ops
/// and adds only a *parallel* skinning stream (joints/weights, 24B) + a skeleton
/// reference. Because the static data is byte-identical and in the same place, a
/// SkinnedMesh can be passed anywhere a StaticMesh& is expected — the static draw
/// path just works; the skinned path additionally binds the skinning stream. The
/// virtual IsSkinned()/SkinningStream() hooks let a consumer holding a StaticMesh&
/// discover + bind the skinning stream without RTTI.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.geometry:mesh;

import draconic.core;
import :types;
import :index_buffer;

using namespace draconic::core;

export namespace draconic::geometry {

class StaticMesh : public Object {
    DRACONIC_OBJECT(StaticMesh, Object)
public:
    String                   name;
    Array<StaticMeshVertex>  vertices;
    IndexBuffer              indices{ IndexBuffer::Format::U32 };
    Array<SubMesh>           subMeshes;
    AABB                     bounds = AABB::Empty();

    [[nodiscard]] u32 VertexCount() const noexcept { return static_cast<u32>(vertices.Size()); }
    [[nodiscard]] u32 IndexCount()  const noexcept { return indices.Count(); }
    [[nodiscard]] static constexpr u32 VertexStride() noexcept { return sizeof(StaticMeshVertex); }

    // Raw static-stream bytes for GPU upload (null when empty).
    [[nodiscard]] const u8* VertexData() const noexcept {
        return vertices.IsEmpty() ? nullptr : reinterpret_cast<const u8*>(vertices.Data());
    }
    [[nodiscard]] u32 VertexDataSize() const noexcept { return VertexCount() * VertexStride(); }

    // Substitutability hooks: a consumer with a StaticMesh& can ask whether it is
    // really skinned and bind the parallel stream, no RTTI needed.
    [[nodiscard]] virtual bool IsSkinned() const noexcept { return false; }
    [[nodiscard]] virtual Span<const VertexSkinning> SkinningStream() const noexcept { return {}; }

    // Resets the mesh to empty in place, so a hot-reload can re-populate the same
    // instance without invalidating outside references (GPU caches, renderers).
    virtual void ClearForReload() {
        name.Clear();
        vertices.Clear();
        indices.Clear();
        subMeshes.Clear();
        bounds = AABB::Empty();
    }

    // Recomputes `bounds` from the static stream.
    AABB& CalculateBounds() {
        if (vertices.IsEmpty()) { bounds = AABB{ Vec3::Zero, Vec3::Zero }; return bounds; }
        bounds = AABB::Empty();
        for (const StaticMeshVertex& v : vertices) { bounds.Expand(v.position); }
        return bounds;
    }

    // Smooth normals: accumulate per-triangle face normals into shared vertices.
    void GenerateNormals() {
        const u32 triangles = TriangleCount();
        if (triangles == 0) { return; }
        for (StaticMeshVertex& v : vertices) { v.normal = Vec3::Zero; }
        for (u32 t = 0; t < triangles; ++t) {
            const u32 i0 = Corner(t, 0), i1 = Corner(t, 1), i2 = Corner(t, 2);
            const Vec3 e1 = vertices[i1].position - vertices[i0].position;
            const Vec3 e2 = vertices[i2].position - vertices[i0].position;
            const Vec3 faceNormal = Cross(e1, e2);
            vertices[i0].normal += faceNormal;
            vertices[i1].normal += faceNormal;
            vertices[i2].normal += faceNormal;
        }
        for (StaticMeshVertex& v : vertices) {
            v.normal = (LengthSquared(v.normal) > 0.0001f) ? Normalized(v.normal) : Vec3::UnitY;
        }
    }

    // Tangents for normal mapping: per-triangle (deltaUV-weighted) accumulation,
    // then Gram-Schmidt orthogonalization against the normal.
    void GenerateTangents() {
        const u32 triangles = TriangleCount();
        if (triangles == 0) { return; }
        for (StaticMeshVertex& v : vertices) { v.tangent = Vec3::Zero; }
        for (u32 t = 0; t < triangles; ++t) {
            const u32 i0 = Corner(t, 0), i1 = Corner(t, 1), i2 = Corner(t, 2);
            const Vec3 dp1 = vertices[i1].position - vertices[i0].position;
            const Vec3 dp2 = vertices[i2].position - vertices[i0].position;
            const Vec2 du1 = vertices[i1].texCoord - vertices[i0].texCoord;
            const Vec2 du2 = vertices[i2].texCoord - vertices[i0].texCoord;
            const f32 denom = du1.x * du2.y - du2.x * du1.y;
            Vec3 tangent = Vec3::Zero;
            if (Abs(denom) > 0.0001f) {
                const f32 r = 1.0f / denom;
                tangent = (dp1 * du2.y - dp2 * du1.y) * r;
            }
            vertices[i0].tangent += tangent;
            vertices[i1].tangent += tangent;
            vertices[i2].tangent += tangent;
        }
        for (StaticMeshVertex& v : vertices) {
            if (LengthSquared(v.tangent) > 0.0001f) {
                v.tangent = v.tangent - v.normal * Dot(v.normal, v.tangent);   // orthogonalize
                v.tangent = (LengthSquared(v.tangent) > 0.0001f) ? Normalized(v.tangent) : DefaultTangent(v.normal);
            } else {
                v.tangent = DefaultTangent(v.normal);
            }
        }
    }

    // Pack a 0..1 float color to RGBA8 with R in the low byte (Unorm8x4 order).
    [[nodiscard]] static u32 PackColor(Vec4 c) {
        const u32 r = static_cast<u32>(Clamp(c.x, 0.0f, 1.0f) * 255.0f);
        const u32 g = static_cast<u32>(Clamp(c.y, 0.0f, 1.0f) * 255.0f);
        const u32 b = static_cast<u32>(Clamp(c.z, 0.0f, 1.0f) * 255.0f);
        const u32 a = static_cast<u32>(Clamp(c.w, 0.0f, 1.0f) * 255.0f);
        return r | (g << 8) | (b << 16) | (a << 24);
    }
    [[nodiscard]] static u32 PackColor(Color32 c) {
        return static_cast<u32>(c.r) | (static_cast<u32>(c.g) << 8)
             | (static_cast<u32>(c.b) << 16) | (static_cast<u32>(c.a) << 24);
    }

protected:
    [[nodiscard]] u32 TriangleCount() const noexcept {
        const bool hasIndices = indices.Count() > 0;
        return hasIndices ? indices.Count() / 3u : VertexCount() / 3u;
    }
    // Vertex index of corner `c` (0..2) of triangle `t` (indexed or sequential).
    [[nodiscard]] u32 Corner(u32 t, u32 c) const noexcept {
        return (indices.Count() > 0) ? indices.Get(t * 3 + c) : (t * 3 + c);
    }
    [[nodiscard]] static Vec3 DefaultTangent(Vec3 normal) {
        const Vec3 t = (Abs(normal.y) < 0.9f) ? Cross(normal, Vec3::UnitY) : Cross(normal, Vec3::UnitX);
        return (LengthSquared(t) > 0.0001f) ? Normalized(t) : Vec3::UnitX;
    }
};

// A skinned mesh: the static stream + a parallel skinning stream + a skeleton ref.
class SkinnedMesh final : public StaticMesh {
    DRACONIC_OBJECT(SkinnedMesh, StaticMesh)
public:
    Array<VertexSkinning> skinning;            // parallel to StaticMesh::vertices
    i32                   skeletonIndex = -1;   // index into the import skeleton list (-1 = none)

    [[nodiscard]] bool IsSkinned() const noexcept override { return true; }
    [[nodiscard]] Span<const VertexSkinning> SkinningStream() const noexcept override {
        return { skinning.Data(), skinning.Size() };
    }

    [[nodiscard]] static constexpr u32 SkinningStride() noexcept { return sizeof(VertexSkinning); }
    [[nodiscard]] const u8* SkinningData() const noexcept {
        return skinning.IsEmpty() ? nullptr : reinterpret_cast<const u8*>(skinning.Data());
    }
    [[nodiscard]] u32 SkinningDataSize() const noexcept { return static_cast<u32>(skinning.Size()) * SkinningStride(); }

    void ClearForReload() override {
        StaticMesh::ClearForReload();
        skinning.Clear();
        skeletonIndex = -1;
    }
};

DRACONIC_DEFINE_OBJECT(StaticMesh, "draconic::geometry")
DRACONIC_DEFINE_OBJECT(SkinnedMesh, "draconic::geometry")

} // namespace draconic::geometry
