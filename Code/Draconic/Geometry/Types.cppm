/// Draconic::Geometry — the `:types` partition.
///
/// Value-type vocabulary for the engine's runtime mesh format (distinct from
/// draconic.model, which is the importer's representation of a loaded file): the
/// primitive topology, a submesh range, and the two vertex streams. A skinned mesh
/// reuses the static vertex stream and adds a *parallel* skinning stream, so its
/// static data is byte-identical to a static mesh (see :mesh).

module;
#include "Core/Prelude.h"

export module draconic.geometry:types;

import draconic.core;

using namespace draconic::core;

export namespace draconic::geometry {

// Primitive topology a submesh is drawn with.
enum class PrimitiveType : u8 {
    Triangles, TriangleStrip, TriangleFan, Lines, LineStrip, Points,
};

// A contiguous index range with its own material + topology.
struct SubMesh {
    i32           startIndex    = 0;
    i32           indexCount    = 0;
    i32           materialIndex = 0;
    PrimitiveType primitiveType = PrimitiveType::Triangles;
};

// The static vertex stream — 48 bytes, matching VertexLayoutType::Mesh (locations
// 0..4). Trivially copyable so the array uploads straight to a GPU vertex buffer.
struct StaticMeshVertex {
    Vector3 position{ 0, 0, 0 };       // 12
    Vector3 normal{ 0, 1, 0 };         // 12
    Vector2 texCoord{ 0, 0 };          //  8
    u32  color = 0xFFFFFFFFu;       //  4  packed RGBA, R in the low byte (Unorm8x4)
    Vector3 tangent{ 1, 0, 0 };        // 12
    // total: 48

    constexpr StaticMeshVertex() noexcept = default;
    constexpr StaticMeshVertex(Vector3 pos, Vector3 nrm, Vector2 uv, u32 col, Vector3 tan) noexcept
        : position(pos), normal(nrm), texCoord(uv), color(col), tangent(tan) {}
};

// The parallel skinning stream — 24 bytes (locations 6/7). One per static vertex; a
// skinned mesh stores this alongside the inherited static stream rather than
// interleaving, so the static stream stays substitutable for a static mesh.
struct VertexSkinning {
    u16  joints[4] = { 0, 0, 0, 0 };    //  8  bone indices (uint16x4, packed uint32x2)
    Vector4 weights{ 1, 0, 0, 0 };         // 16  bone weights (sum to 1)
    // total: 24
};

static_assert(sizeof(StaticMeshVertex) == 48, "static vertex must stay 48 bytes (VertexLayoutType::Mesh)");
static_assert(sizeof(VertexSkinning) == 24, "skinning stream must stay 24 bytes (locations 6/7)");

} // namespace draconic::geometry
