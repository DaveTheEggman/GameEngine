/// Standard and skinned vertex structs for model data.
/// Ported from Sedulous.Models/ModelVertex.bf.

export module draconic.model:model_vertex;

import draconic.core;

using namespace draconic::core;

export namespace draconic::model {

/// Standard vertex format for static models (48 bytes).
struct ModelVertex {
    Vec3 position{};             // 12 bytes
    Vec3 normal{ 0, 1, 0 };     // 12 bytes
    Vec2 texCoord{};             // 8 bytes
    u32  color = 0xFFFFFFFF;     // 4 bytes (packed RGBA)
    Vec3 tangent{ 1, 0, 0 };    // 12 bytes
    // Total: 48 bytes

    constexpr ModelVertex() = default;
    constexpr ModelVertex(Vec3 pos, Vec3 nrm, Vec2 uv, u32 col = 0xFFFFFFFF, Vec3 tan = { 1, 0, 0 })
        : position(pos), normal(nrm), texCoord(uv), color(col), tangent(tan) {}
};

static_assert(sizeof(ModelVertex) == 48, "ModelVertex must be 48 bytes");

/// Skinned vertex format for animated models (72 bytes).
struct SkinnedModelVertex {
    Vec3 position{};             // 12 bytes
    Vec3 normal{ 0, 1, 0 };     // 12 bytes
    Vec2 texCoord{};             // 8 bytes
    u32  color = 0xFFFFFFFF;     // 4 bytes (packed RGBA)
    Vec3 tangent{ 1, 0, 0 };    // 12 bytes
    u16  joints[4] = { 0, 0, 0, 0 }; // 8 bytes (up to 4 bone indices)
    Vec4 weights{ 1, 0, 0, 0 };      // 16 bytes (bone weights)
    // Total: 72 bytes

    constexpr SkinnedModelVertex() = default;
    constexpr SkinnedModelVertex(Vec3 pos, Vec3 nrm, Vec2 uv, u32 col, Vec3 tan,
                                 u16 j0, u16 j1, u16 j2, u16 j3, Vec4 wt)
        : position(pos), normal(nrm), texCoord(uv), color(col), tangent(tan),
          joints{ j0, j1, j2, j3 }, weights(wt) {}
};

static_assert(sizeof(SkinnedModelVertex) == 72, "SkinnedModelVertex must be 72 bytes");

} // namespace draconic::model
