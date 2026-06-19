/// Material properties for PBR rendering.
/// Ported from Sedulous.Models/ModelMaterial.bf.

module;
#include "Core/Prelude.h"

#include <string>

export module raptor.model:model_material;

import raptor.core;

using namespace raptor::core;

export namespace raptor::model {

/// Alpha blending mode.
enum class AlphaMode : u32 {
    Opaque,
    Mask,
    Blend,
};

/// PBR material properties for a model.
class ModelMaterial {
public:
    ModelMaterial() = default;
    ~ModelMaterial() = default;

    [[nodiscard]] WideStringView name() const { return WideStringView(m_name.Data(), m_name.Size()); }
    void setName(WideStringView n) { m_name = WideString(n); }

    // -- Base color --
    Vec4 baseColorFactor{ 1, 1, 1, 1 };
    i32  baseColorTextureIndex = -1;

    // -- Metallic-Roughness --
    f32 metallicFactor  = 1.0f;
    f32 roughnessFactor = 1.0f;
    i32 metallicRoughnessTextureIndex = -1;

    // -- Normal map --
    f32 normalScale = 1.0f;
    i32 normalTextureIndex = -1;

    // -- Occlusion --
    f32 occlusionStrength = 1.0f;
    i32 occlusionTextureIndex = -1;

    // -- Emissive --
    Vec3 emissiveFactor{};
    i32  emissiveTextureIndex = -1;

    // -- Alpha --
    AlphaMode alphaMode = AlphaMode::Opaque;
    f32 alphaCutoff = 0.5f;

    // -- Double-sided --
    bool doubleSided = false;

private:
    WideString m_name;
};

} // namespace raptor::model
