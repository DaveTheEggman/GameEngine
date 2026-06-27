/// Raptor::Materials — the `:builder` partition.
///
/// Fluent builder for authoring a Material in code: declare the shader, pipeline
/// state, and typed properties; the builder lays out the uniform buffer (std140-ish
/// 16-byte alignment for float3/float4) and assigns binding ordinals. Build() hands
/// back the finished Material (RefPtr, since Material is an Object).

module;
#include "Core/Prelude.h"

export module raptor.materials:builder;

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import :types;
import :pipeline;
import :material;

using namespace raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::materials {

class MaterialBuilder {
public:
    explicit MaterialBuilder(StringView name) {
        m_material = MakeRef<Material>(DefaultAllocator());
        m_material->name = String(name);
        m_material->pipeline = PipelineConfig{};
    }

    MaterialBuilder& Shader(StringView shaderName) {
        m_material->shaderName = String(shaderName);
        m_material->pipeline.shaderName = m_material->shaderName.AsView();
        return *this;
    }
    MaterialBuilder& Flags(shaders::ShaderFlags flags) {
        m_material->shaderFlags = flags;
        m_material->pipeline.shaderFlags = flags;
        return *this;
    }
    MaterialBuilder& VertexLayout(VertexLayoutType layout) { m_material->pipeline.vertexLayout = layout; return *this; }
    MaterialBuilder& Blend(BlendMode mode)                 { m_material->pipeline.blendMode = mode; return *this; }
    MaterialBuilder& Depth(DepthMode mode)                 { m_material->pipeline.depthMode = mode; return *this; }
    MaterialBuilder& Cull(CullModeConfig mode)             { m_material->pipeline.cullMode = mode; return *this; }
    MaterialBuilder& DoubleSided()                         { m_material->pipeline.cullMode = CullModeConfig::None; return *this; }
    MaterialBuilder& Transparent() {
        m_material->pipeline.blendMode = BlendMode::AlphaBlend;
        m_material->pipeline.depthMode = DepthMode::ReadOnly;
        return *this;
    }
    MaterialBuilder& Additive() {
        m_material->pipeline.blendMode = BlendMode::Additive;
        m_material->pipeline.depthMode = DepthMode::ReadOnly;
        return *this;
    }

    // --- uniform properties (laid out into the material uniform buffer) ---
    MaterialBuilder& Float(StringView name, f32 v = 0.0f) {
        AddUniform(name, MaterialPropertyType::Float, 4, /*align16*/ false);
        m_material->AllocateDefaultUniformData();
        m_material->SetDefaultFloat(name, v);
        return *this;
    }
    MaterialBuilder& Float2(StringView name, Vec2 v = {}) {
        AddUniform(name, MaterialPropertyType::Float2, 8, false);
        m_material->AllocateDefaultUniformData();
        m_material->SetDefaultFloat2(name, v);
        return *this;
    }
    MaterialBuilder& Float3(StringView name, Vec3 v = {}) {
        AddUniform(name, MaterialPropertyType::Float3, 12, /*align16*/ true);   // float3 occupies 16 (std140)
        m_material->AllocateDefaultUniformData();
        m_material->SetDefaultFloat3(name, v);
        return *this;
    }
    MaterialBuilder& Float4(StringView name, Vec4 v = {}) {
        AddUniform(name, MaterialPropertyType::Float4, 16, true);
        m_material->AllocateDefaultUniformData();
        m_material->SetDefaultFloat4(name, v);
        return *this;
    }
    MaterialBuilder& Color(StringView name, Vec4 v = Vec4{ 1, 1, 1, 1 }) { return Float4(name, v); }

    // --- resource properties (become bind-group entries) ---
    MaterialBuilder& Texture(StringView name, rhi::TextureView* def = nullptr) {
        m_material->AddProperty(MaterialPropertyDef{ name, MaterialPropertyType::Texture2D, m_binding++, 0, 0 });
        if (def != nullptr) { m_material->SetDefaultTexture(name, def); }
        return *this;
    }
    MaterialBuilder& TextureCube(StringView name, rhi::TextureView* def = nullptr) {
        m_material->AddProperty(MaterialPropertyDef{ name, MaterialPropertyType::TextureCube, m_binding++, 0, 0 });
        if (def != nullptr) { m_material->SetDefaultTexture(name, def); }
        return *this;
    }
    MaterialBuilder& Sampler(StringView name, rhi::Sampler* def = nullptr) {
        m_material->AddProperty(MaterialPropertyDef{ name, MaterialPropertyType::Sampler, m_binding++, 0, 0 });
        if (def != nullptr) { m_material->SetDefaultSampler(name, def); }
        return *this;
    }

    // Finishes and yields the material. The builder is empty afterwards.
    [[nodiscard]] RefPtr<Material> Build() {
        m_material->AllocateDefaultUniformData();
        return Move(m_material);
    }

private:
    void AddUniform(StringView name, MaterialPropertyType type, u32 size, bool align16) {
        if (align16) { m_uniformOffset = (m_uniformOffset + 15u) & ~15u; }
        m_material->AddProperty(MaterialPropertyDef{ name, type, m_binding, m_uniformOffset, size });
        m_uniformOffset += align16 ? 16u : size;   // float3/float4 consume a 16-byte slot
        ++m_binding;
    }

    RefPtr<Material> m_material;
    u32 m_uniformOffset = 0;
    u32 m_binding = 0;
};

} // namespace raptor::materials
