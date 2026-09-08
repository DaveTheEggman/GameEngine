// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Foundation::Materials.Resource - the `foundation.materials.resource` module.
///
/// Materials as resources: a `MaterialSource` (authored content - references a shader
/// by Guid, plus declared properties + render-state presets + the default-uniform
/// blob) is built by `MaterialFactory` into a runtime `Material`. The factory Binds
/// the referenced `ShaderResource` mid-build, which AUTOMATICALLY records a
/// material→shader dependency edge (see foundation.resource): reloading the shader
/// transitively reloads the material. The product is the data-only `Material`; its
/// GPU bind-group layout is still inferred later by the MaterialSystem.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

export module foundation.materials.resource;

import foundation.core;
import foundation.rhi;
import foundation.resource;
import foundation.content;
import foundation.shaders;
import foundation.shaders.resource;
import foundation.texture;
import foundation.texture.resource;
import foundation.materials;

using namespace foundation::core;
using namespace foundation::resource;
namespace rhi = foundation::rhi;
namespace shaders = foundation::shaders;
namespace texture = foundation::texture;

export namespace foundation::materials
{

    // Authored material: a shader reference (by resource id) + declared properties (as
    // parallel arrays so it serializes with the primitive Array<T> path) + render-state
    // presets + the packed default-uniform blob. Texture/sampler defaults are resolved at
    // runtime (white/normal fallbacks), so they are not stored.
    class MaterialSource final : public ISerializable
    {
        RTTI_OBJECT(MaterialSource, ISerializable)
    public:
        String name;
        Guid shaderId;     // a cooked ShaderResource; nil -> use shaderName (a builtin)
        String shaderName; // builtin shader name fallback (when shaderId is nil)
        u32 shaderFlags = 0;
        BlendMode blendMode = BlendMode::Opaque;
        DepthMode depthMode = DepthMode::ReadWrite;
        CullModeConfig cullMode = CullModeConfig::Back;
        VertexLayoutType vertexLayout = VertexLayoutType::Mesh;
        // Sampler address modes (rhi::AddressMode values: 0 Repeat, 1 MirrorRepeat,
        // 2 ClampToEdge) - wired from the source asset's sampler at import (v2).
        u8 samplerU = 0;
        u8 samplerV = 0;

        Array<String> propertyNames;
        Array<u8> propertyTypes; // MaterialPropertyType
        Array<u32> propertyBindings;
        Array<u32> propertyOffsets;
        Array<u32> propertySizes;
        Array<u8> uniformDefaults;

        // Default texture bindings: slot name -> texture product guid (parallel arrays). The
        // factory resolves each through the manager (recording the dependency edge, so a texture
        // hot reload cascades into this material) and sets it as the material's default texture.
        // This makes a cooked material SELF-CONTAINED - previously only the model-spawn composite
        // wired textures, so a directly-referenced material rendered untextured.
        Array<String> textureSlots;
        Array<Guid> textureIds;

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "name", name);
            foundation::core::Serialize(ar, "shaderId", shaderId);
            foundation::core::Serialize(ar, "shaderName", shaderName);
            foundation::core::Serialize(ar, "shaderFlags", shaderFlags);
            foundation::core::Serialize(ar, "blendMode", blendMode);
            foundation::core::Serialize(ar, "depthMode", depthMode);
            foundation::core::Serialize(ar, "cullMode", cullMode);
            foundation::core::Serialize(ar, "vertexLayout", vertexLayout);
            foundation::core::Serialize(ar, "propertyNames", propertyNames);
            foundation::core::Serialize(ar, "propertyTypes", propertyTypes);
            foundation::core::Serialize(ar, "propertyBindings", propertyBindings);
            foundation::core::Serialize(ar, "propertyOffsets", propertyOffsets);
            foundation::core::Serialize(ar, "propertySizes", propertySizes);
            foundation::core::Serialize(ar, "uniformDefaults", uniformDefaults);
            foundation::core::Serialize(ar, "textureSlots", textureSlots);
            foundation::core::Serialize(ar, "textureIds", textureIds);
            foundation::core::Serialize(ar, "samplerU", samplerU);
            foundation::core::Serialize(ar, "samplerV", samplerV);
        }

        // Captures a built Material's declared layout + defaults into an authorable source
        // referencing `shaderId`. (Used by the editor cook and round-trip tests.)
        static void FromMaterial(const Material& material, const Guid& shaderId,
                                 MaterialSource& out)
        {
            out.name = String(material.name.AsView());
            out.shaderId = shaderId;
            out.shaderName = String(material.shaderName.AsView());
            out.shaderFlags = static_cast<u32>(material.shaderFlags);
            out.blendMode = material.pipeline.blendMode;
            out.depthMode = material.pipeline.depthMode;
            out.cullMode = material.pipeline.cullMode;
            out.vertexLayout = material.pipeline.vertexLayout;
            out.samplerU = static_cast<u8>(material.samplerU);
            out.samplerV = static_cast<u8>(material.samplerV);

            out.propertyNames.Clear();
            out.propertyTypes.Clear();
            out.propertyBindings.Clear();
            out.propertyOffsets.Clear();
            out.propertySizes.Clear();
            for (const MaterialPropertyDef& d : material.Properties())
            {
                out.propertyNames.PushBack(String(d.name));
                out.propertyTypes.PushBack(static_cast<u8>(d.type));
                out.propertyBindings.PushBack(d.binding);
                out.propertyOffsets.PushBack(d.offset);
                out.propertySizes.PushBack(d.size);
            }
            const Span<const u8> defaults = material.DefaultUniformData();
            out.uniformDefaults.Clear();
            out.uniformDefaults.Reserve(defaults.Size());
            for (usize i = 0; i < defaults.Size(); ++i)
            {
                out.uniformDefaults.PushBack(defaults[i]);
            }
        }
    };

    // Builds a MaterialSource into a runtime Material, binding the referenced shader
    // resource (which records the dependency edge) to resolve its name.
    // A "forward" material source must carry the forward shader's full set-2 property table
    // (EmissiveColor @32, OcclusionStrength/NormalScale/AlphaCutoff @48/52/56): without them the
    // uniform buffer is short and the shader reads past it. Sources are never upgraded in
    // memory - a source missing one is stale and is refused (re-create the material). New
    // CreatePBR sources carry them; unlit and custom shaders are untouched.
    [[nodiscard]] inline bool ForwardMaterialSourceIsComplete(const MaterialSource& src,
                                                              StringView* outMissing = nullptr)
    {
        if (src.shaderName != u8"forward")
        {
            return true;
        }
        static constexpr StringView kRequired[] = {u8"EmissiveColor", u8"OcclusionStrength",
                                                   u8"NormalScale", u8"AlphaCutoff"};
        for (StringView required : kRequired)
        {
            bool present = false;
            for (const String& n : src.propertyNames)
            {
                if (n.AsView() == required)
                {
                    present = true;
                    break;
                }
            }
            if (!present)
            {
                if (outMissing != nullptr)
                {
                    *outMissing = required;
                }
                return false;
            }
        }
        return true;
    }

    class MaterialFactory final : public IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &Material::StaticType();
        }

        [[nodiscard]] RefPtr<Object> Create(ResourceManager& manager,
                                            foundation::content::Instance& instance) override
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            MaterialSource* src = Cast<MaterialSource>(object.Get());
            if (src == nullptr)
            {
                return RefPtr<Object>{};
            }
            StringView missing;
            if (!ForwardMaterialSourceIsComplete(*src, &missing))
            {
                LOG_ERROR(u8"Materials",
                          u8"material '{}' is missing the forward shader property '{}' (a stale "
                          u8"source) - re-create the material",
                          src->name, missing);
                return RefPtr<Object>{};
            }

            // Resolve the shader: a cooked ShaderResource (by id, recording the material->shader edge)
            // or a builtin shader named directly (shaderName) when no resource id is given.
            String shaderName;
            if (!src->shaderId.IsNil())
            {
                Proxy<shaders::ShaderResource> shader =
                    manager.Bind<shaders::ShaderResource>(src->shaderId);
                if (shader)
                {
                    shaderName = String(shader->Name());
                }
            }
            if (shaderName.IsEmpty())
            {
                shaderName = String(src->shaderName.AsView());
            }

            RefPtr<Material> material = MakeRef<Material>(DefaultAllocator());
            material->name = String(src->name.AsView());
            material->shaderName = Move(shaderName);
            material->shaderFlags = static_cast<shaders::ShaderFlags>(src->shaderFlags);

            const usize count = src->propertyNames.Size();
            for (usize i = 0; i < count; ++i)
            {
                MaterialPropertyDef d{};
                d.name = src->propertyNames[i].AsView();
                d.type = static_cast<MaterialPropertyType>(src->propertyTypes[i]);
                d.binding = (i < src->propertyBindings.Size()) ? src->propertyBindings[i] : 0u;
                d.offset = (i < src->propertyOffsets.Size()) ? src->propertyOffsets[i] : 0u;
                d.size = (i < src->propertySizes.Size()) ? src->propertySizes[i] : 0u;
                material->AddProperty(d);
            }
            material->AllocateDefaultUniformData();
            material->SetRawDefaultUniformData(
                Span<const u8>{src->uniformDefaults.Data(), src->uniformDefaults.Size()});

            material->pipeline = PipelineConfig{};
            material->pipeline.shaderName = material->shaderName.AsView();
            material->pipeline.shaderFlags = material->shaderFlags;
            material->pipeline.blendMode = src->blendMode;
            material->pipeline.depthMode = src->depthMode;
            material->pipeline.cullMode = src->cullMode;
            material->pipeline.vertexLayout = src->vertexLayout;
            material->samplerU = static_cast<rhi::AddressMode>(src->samplerU);
            material->samplerV = static_cast<rhi::AddressMode>(src->samplerV);

            // Default texture bindings: resolve each through the manager (the Bind records the
            // material->texture edge, so a texture reload rebuilds this material) and install as
            // the material's default. Missing textures (not cooked yet, no GPU factory in
            // headless tools) just leave the slot unbound.
            for (usize i = 0; i < src->textureSlots.Size() && i < src->textureIds.Size(); ++i)
            {
                if (src->textureIds[i].IsNil())
                {
                    continue;
                }
                // Route by the async flag (the scope the scene resolve sets): a direct
                // manager.Bind never consults it, and THIS loop was the serial-load stall -
                // 69 Sponza textures decoded on the UI thread inside material builds.
                Proxy<texture::Texture> tex =
                    manager.AsyncBindsEnabled()
                        ? manager.BindAsync<texture::Texture>(src->textureIds[i])
                        : manager.Bind<texture::Texture>(src->textureIds[i]);
                if (!tex)
                {
                    if (tex.Handle() != nullptr &&
                        tex.Handle()->State() == ResourceState::Pending)
                    {
                        // Decoding on a worker: the slot stays unbound QUIETLY - the settle
                        // reloads this material through the recorded edge and fills it.
                        continue;
                    }
                    LOG_WARNING(u8"Materials",
                                         u8"material '{}': texture for slot '{}' failed to bind "
                                         u8"(no product / no texture factory?)",
                                         src->name, src->textureSlots[i]);
                    continue;
                }
                if (tex->View() == nullptr)
                {
                    LOG_WARNING(u8"Materials",
                                         u8"material '{}': texture for slot '{}' has no GPU view",
                                         src->name, src->textureSlots[i]);
                    continue;
                }
                if (material->FindProperty(src->textureSlots[i].AsView()) == nullptr)
                {
                    LOG_WARNING(
                        u8"Materials",
                        u8"material '{}': no texture property named '{}' in its layout", src->name,
                        src->textureSlots[i]);
                    continue;
                }
                material->SetDefaultTexture(src->textureSlots[i].AsView(), tex->View());
                LOG_DEBUG(u8"Materials", u8"material '{}': slot '{}' bound", src->name,
                                   src->textureSlots[i]);
            }
            return material;
        }
    };

    // MaterialSource::StaticType() is defined WITH its reflected properties + data version (2)
    // in MaterialResourceImpl.cpp (GCC module hygiene: REFLECT_MEMBERS bodies out of interfaces).

} // namespace foundation::materials
