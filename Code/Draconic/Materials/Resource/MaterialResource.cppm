/// Draconic::MaterialResource - the `draconic.materials.resource` module.
///
/// Materials as resources: a `MaterialSource` (authored content - references a shader
/// by Guid, plus declared properties + render-state presets + the default-uniform
/// blob) is built by `MaterialFactory` into a runtime `Material`. The factory Binds
/// the referenced `ShaderResource` mid-build, which AUTOMATICALLY records a
/// material→shader dependency edge (see draconic.resource): reloading the shader
/// transitively reloads the material. The product is the data-only `Material`; its
/// GPU bind-group layout is still inferred later by the MaterialSystem.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

export module draconic.materials.resource;

import draconic.core;
import draconic.resource;
import draconic.content;
import draconic.shaders;
import draconic.shaders.resource;
import draconic.texture;
import draconic.texture.resource;
import draconic.materials;

using namespace draconic::core;
using namespace draconic::resource;

export namespace draconic::materials {

// Authored material: a shader reference (by resource id) + declared properties (as
// parallel arrays so it serializes with the primitive Array<T> path) + render-state
// presets + the packed default-uniform blob. Texture/sampler defaults are resolved at
// runtime (white/normal fallbacks), so they are not stored.
class MaterialSource final : public ISerializable {
    DRACONIC_OBJECT(MaterialSource, ISerializable)
public:
    String name;
    Guid   shaderId;             // a cooked ShaderResource; nil -> use shaderName (a builtin)
    String shaderName;           // builtin shader name fallback (when shaderId is nil)
    u32    shaderFlags  = 0;
    u8     blendMode    = static_cast<u8>(BlendMode::Opaque);
    u8     depthMode    = static_cast<u8>(DepthMode::ReadWrite);
    u8     cullMode     = static_cast<u8>(CullModeConfig::Back);
    u8     vertexLayout = static_cast<u8>(VertexLayoutType::Mesh);

    Array<String> propNames;
    Array<u8>     propTypes;      // MaterialPropertyType
    Array<u32>    propBindings;
    Array<u32>    propOffsets;
    Array<u32>    propSizes;
    Array<u8>     uniformDefaults;

    // Default texture bindings: slot name -> texture product guid (parallel arrays). The
    // factory resolves each through the manager (recording the dependency edge, so a texture
    // hot reload cascades into this material) and sets it as the material's default texture.
    // This makes a cooked material SELF-CONTAINED - previously only the model-spawn composite
    // wired textures, so a directly-referenced material rendered untextured.
    Array<String> textureSlots;
    Array<Guid>   textureIds;

    void Serialize(ISerializer& ar) override {
        draconic::core::Serialize(ar, "name", name);
        draconic::core::Serialize(ar, "shaderId", shaderId);
        draconic::core::Serialize(ar, "shaderName", shaderName);
        draconic::core::Serialize(ar, "shaderFlags", shaderFlags);
        draconic::core::Serialize(ar, "blendMode", blendMode);
        draconic::core::Serialize(ar, "depthMode", depthMode);
        draconic::core::Serialize(ar, "cullMode", cullMode);
        draconic::core::Serialize(ar, "vertexLayout", vertexLayout);
        draconic::core::Serialize(ar, "propNames", propNames);
        draconic::core::Serialize(ar, "propTypes", propTypes);
        draconic::core::Serialize(ar, "propBindings", propBindings);
        draconic::core::Serialize(ar, "propOffsets", propOffsets);
        draconic::core::Serialize(ar, "propSizes", propSizes);
        draconic::core::Serialize(ar, "uniformDefaults", uniformDefaults);
        draconic::core::Serialize(ar, "textureSlots", textureSlots);
        draconic::core::Serialize(ar, "textureIds", textureIds);
    }

    // Captures a built Material's declared layout + defaults into an authorable source
    // referencing `shaderId`. (Used by the editor cook and round-trip tests.)
    static void FromMaterial(const Material& material, const Guid& shaderId, MaterialSource& out) {
        out.name = String(material.name.AsView());
        out.shaderId = shaderId;
        out.shaderName = String(material.shaderName.AsView());
        out.shaderFlags = static_cast<u32>(material.shaderFlags);
        out.blendMode    = static_cast<u8>(material.pipeline.blendMode);
        out.depthMode    = static_cast<u8>(material.pipeline.depthMode);
        out.cullMode     = static_cast<u8>(material.pipeline.cullMode);
        out.vertexLayout = static_cast<u8>(material.pipeline.vertexLayout);

        out.propNames.Clear(); out.propTypes.Clear(); out.propBindings.Clear();
        out.propOffsets.Clear(); out.propSizes.Clear();
        for (const MaterialPropertyDef& d : material.Properties()) {
            out.propNames.PushBack(String(d.name));
            out.propTypes.PushBack(static_cast<u8>(d.type));
            out.propBindings.PushBack(d.binding);
            out.propOffsets.PushBack(d.offset);
            out.propSizes.PushBack(d.size);
        }
        const Span<const u8> defaults = material.DefaultUniformData();
        out.uniformDefaults.Clear();
        out.uniformDefaults.Reserve(defaults.Size());
        for (usize i = 0; i < defaults.Size(); ++i) { out.uniformDefaults.PushBack(defaults[i]); }
    }
};

// Builds a MaterialSource into a runtime Material, binding the referenced shader
// resource (which records the dependency edge) to resolve its name.
class MaterialFactory final : public IResourceFactory {
public:
    [[nodiscard]] const TypeInfo* ProductType() const override { return &Material::StaticType(); }

    [[nodiscard]] RefPtr<Object> Create(ResourceManager& manager, draconic::content::Instance& instance) override {
        RefPtr<ISerializable> object = instance.ReadObject();
        MaterialSource* src = Cast<MaterialSource>(object.Get());
        if (src == nullptr) { return RefPtr<Object>{}; }

        // Resolve the shader: a cooked ShaderResource (by id, recording the material->shader edge)
        // or a builtin shader named directly (shaderName) when no resource id is given.
        String shaderName;
        if (!src->shaderId.IsNil()) {
            Proxy<shaders::ShaderResource> shader = manager.Bind<shaders::ShaderResource>(src->shaderId);
            if (shader) { shaderName = String(shader->Name()); }
        }
        if (shaderName.IsEmpty()) { shaderName = String(src->shaderName.AsView()); }

        RefPtr<Material> material = MakeRef<Material>(DefaultAllocator());
        material->name = String(src->name.AsView());
        material->shaderName = Move(shaderName);
        material->shaderFlags = static_cast<shaders::ShaderFlags>(src->shaderFlags);

        const usize count = src->propNames.Size();
        for (usize i = 0; i < count; ++i) {
            MaterialPropertyDef d{};
            d.name    = src->propNames[i].AsView();
            d.type    = static_cast<MaterialPropertyType>(src->propTypes[i]);
            d.binding = (i < src->propBindings.Size()) ? src->propBindings[i] : 0u;
            d.offset  = (i < src->propOffsets.Size())  ? src->propOffsets[i]  : 0u;
            d.size    = (i < src->propSizes.Size())    ? src->propSizes[i]    : 0u;
            material->AddProperty(d);
        }
        material->AllocateDefaultUniformData();
        material->SetRawDefaultUniformData(Span<const u8>{ src->uniformDefaults.Data(), src->uniformDefaults.Size() });

        material->pipeline = PipelineConfig{};
        material->pipeline.shaderName   = material->shaderName.AsView();
        material->pipeline.shaderFlags  = material->shaderFlags;
        material->pipeline.blendMode    = static_cast<BlendMode>(src->blendMode);
        material->pipeline.depthMode    = static_cast<DepthMode>(src->depthMode);
        material->pipeline.cullMode     = static_cast<CullModeConfig>(src->cullMode);
        material->pipeline.vertexLayout = static_cast<VertexLayoutType>(src->vertexLayout);

        // Default texture bindings: resolve each through the manager (the Bind records the
        // material->texture edge, so a texture reload rebuilds this material) and install as
        // the material's default. Missing textures (not cooked yet, no GPU factory in
        // headless tools) just leave the slot unbound.
        for (usize i = 0; i < src->textureSlots.Size() && i < src->textureIds.Size(); ++i) {
            if (src->textureIds[i].IsNil()) { continue; }
            Proxy<texture::Texture> tex = manager.Bind<texture::Texture>(src->textureIds[i]);
            if (!tex) {
                DRACONIC_LOG_WARNING(u8"Materials", u8"material '{}': texture for slot '{}' failed to bind (no product / no texture factory?)",
                                     src->name, src->textureSlots[i]);
                continue;
            }
            if (tex->View() == nullptr) {
                DRACONIC_LOG_WARNING(u8"Materials", u8"material '{}': texture for slot '{}' has no GPU view",
                                     src->name, src->textureSlots[i]);
                continue;
            }
            if (material->FindProperty(src->textureSlots[i].AsView()) == nullptr) {
                DRACONIC_LOG_WARNING(u8"Materials", u8"material '{}': no texture property named '{}' in its layout",
                                     src->name, src->textureSlots[i]);
                continue;
            }
            material->SetDefaultTexture(src->textureSlots[i].AsView(), tex->View());
            DRACONIC_LOG_DEBUG(u8"Materials", u8"material '{}': slot '{}' bound", src->name, src->textureSlots[i]);
        }
        return material;
    }
};

DRACONIC_DEFINE_OBJECT(MaterialSource, "draconic::materials")

} // namespace draconic::materials
