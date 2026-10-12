// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Pipeline::Materials - the `foundation.materials.editor` module (tooling).
//
// Source-side material authoring + cook:
//   * MaterialAsset (pipeline::Asset): wraps a MaterialSource (the authored material -
//     shader reference + declared properties + render-state presets + default
//     uniforms). A material has no external source file to decode, so the asset IS
//     the authored data; cooking resolves it into the product DB.
//   * MaterialAssetBuilder (DefaultAssetBuilder): writes the resolved MaterialSource
//     into the output DB (where MaterialFactory later builds it into a Material).
//   * MaterialImporter: fills a MaterialAsset from a Material authored in code + the
//     shader resource id it references.
//
// Never linked by the runtime.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include "Core/Log/Log.h"

export module materials.pipeline;

import foundation.core;
import pipeline.core;
import foundation.content;
import foundation.materials;
import foundation.materials.resource;
import foundation.shaders;          // ShaderProperty: a custom shader's property table
import foundation.shaders.resource; // ShaderSource: the cooked custom shader a material reads

using namespace foundation::core;
using namespace foundation::materials;

export namespace pipeline{

    // Source asset wrapping the authored material data.
    class MaterialAsset final : public pipeline::Asset
    {
        RTTI_OBJECT(MaterialAsset, pipeline::Asset)
    public:
        MaterialSource source;

        void Serialize(ISerializer& ar) override
        {
            pipeline::Asset::Serialize(ar); // fileName (optional authoring note)
            source.Serialize(ar);
        }
    };

    // Cooks a MaterialAsset -> MaterialSource in the output DB (the authored source is
    // already the runtime source; writing it is the cook).
    class MaterialAssetBuilder final : public pipeline::DefaultAssetBuilder
    {
    public:
        // Cook version 2 (2026-09-08): MaterialSource 3 - the property arrays use full names; a product cooked under the older record
        // version is refused by the reader, so the recipe hash must change to re-cook it.
        // Version 3: a material on a custom shader is laid out by the shader's property table.
        [[nodiscard]] u32 Version() const override { return 3; }

        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &MaterialAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &MaterialSource::StaticType();
        }

        // Bound textures are runtime REFERENCES: their products must exist, but a texture edit
        // never re-cooks the material (the factory re-binds at load/reload).
        void ScanDependencies(const pipeline::Asset& asset,
                              pipeline::AssetBuildContext&,
                              pipeline::AssetDependencies& out) override
        {
            const MaterialAsset& ma = static_cast<const MaterialAsset&>(asset);
            for (const Guid& id : ma.source.textureIds)
            {
                if (!id.IsNil())
                {
                    out.references.PushBack(id);
                }
            }
            // A custom shader's property table lays the product out: its CONTENT is read, so a
            // shader edit (a property added, removed or retyped) re-cooks the material.
            if (!ma.source.shaderId.IsNil())
            {
                out.reads.PushBack(ma.source.shaderId);
            }
        }

        [[nodiscard]] Status Build(const pipeline::Asset& asset,
                                   pipeline::AssetBuildContext& ctx) override
        {
            const MaterialAsset& ma =
                static_cast<const MaterialAsset&>(asset); // guarded by AssetType()
            if (ctx.output == nullptr)
            {
                return Status{ErrorCode::InvalidArgument};
            }
            if (ma.source.shaderId.IsNil())
            {
                // A built-in shader: the authored source is the runtime source. WriteObject only
                // reads during a write pass; const_cast is safe here.
                return ctx.output->WriteObject(const_cast<MaterialSource&>(ma.source));
            }

            // A custom shader: its cooked property table is the layout, the asset the values.
            foundation::content::Instance* instance =
                ctx.db != nullptr ? ctx.db->GetInstance(ma.source.shaderId) : nullptr;
            RefPtr<ISerializable> object =
                instance != nullptr ? instance->ReadObject() : RefPtr<ISerializable>{};
            const auto* shader = Cast<foundation::shaders::ShaderSource>(object.Get());
            if (shader == nullptr)
            {
                LOG_ERROR(u8"Materials", u8"material '{}': its shader has not cooked - cook failed",
                          ma.source.name);
                return Status{ErrorCode::NotFound};
            }
            MaterialSource product;
            LayOutByShader(ma.source, *shader, product);
            return ctx.output->WriteObject(product);
        }

        // A material on a custom shader, laid out by the shader's property table: each property
        // in the shader's order and at its offset, its value taken from the authored material by
        // name where the type matches, else the shader's default. An authored value the shader no
        // longer declares is dropped with a warning; so is a texture binding to a slot it lacks.
        static void LayOutByShader(const MaterialSource& authored,
                                   const foundation::shaders::ShaderSource& shader,
                                   MaterialSource& out)
        {
            namespace sh = foundation::shaders;
            // Name, shader reference, render state and samplers come from the authored material.
            out.name = authored.name;
            out.shaderId = authored.shaderId;
            out.shaderName = shader.name;
            out.shaderFlags = authored.shaderFlags;
            out.blendMode = authored.blendMode;
            out.depthMode = authored.depthMode;
            out.cullMode = authored.cullMode;
            out.vertexLayout = authored.vertexLayout;
            out.samplerU = authored.samplerU;
            out.samplerV = authored.samplerV;
            out.propertyNames.Clear();
            out.propertyTypes.Clear();
            out.propertyBindings.Clear();
            out.propertyOffsets.Clear();
            out.propertySizes.Clear();
            out.textureSlots.Clear();
            out.textureIds.Clear();
            out.uniformDefaults.Clear();
            out.uniformDefaults.Resize(shader.uniformSize);
            for (u8& b : out.uniformDefaults)
            {
                b = 0;
            }

            const Array<sh::ShaderProperty> table = shader.Properties();
            for (usize i = 0; i < table.Size(); ++i)
            {
                const sh::ShaderProperty& p = table[i];
                const MaterialPropertyType type = MaterialTypeOf(p.type);
                out.propertyNames.PushBack(p.name);
                out.propertyTypes.PushBack(static_cast<u8>(type));
                out.propertyBindings.PushBack(static_cast<u32>(i));
                out.propertyOffsets.PushBack(p.offset);
                out.propertySizes.PushBack(p.size);

                const isize authoredIndex = IndexOf(authored.propertyNames, p.name.AsView());
                if (p.size == 0u)
                {
                    // A texture keeps its authored binding when the slot is still a texture.
                    if (p.type == sh::ShaderPropertyType::Sampler)
                    {
                        continue;
                    }
                    const isize slot = IndexOf(authored.textureSlots, p.name.AsView());
                    if (slot >= 0 && static_cast<usize>(slot) < authored.textureIds.Size())
                    {
                        out.textureSlots.PushBack(p.name);
                        out.textureIds.PushBack(authored.textureIds[static_cast<usize>(slot)]);
                    }
                    continue;
                }
                const bool sameKind =
                    authoredIndex >= 0 &&
                    static_cast<usize>(authoredIndex) < authored.propertyTypes.Size() &&
                    static_cast<usize>(authoredIndex) < authored.propertyOffsets.Size() &&
                    static_cast<usize>(authoredIndex) < authored.propertySizes.Size() &&
                    authored.propertyTypes[static_cast<usize>(authoredIndex)] ==
                        static_cast<u8>(type) &&
                    authored.propertySizes[static_cast<usize>(authoredIndex)] == p.size;
                const u32 from =
                    sameKind ? authored.propertyOffsets[static_cast<usize>(authoredIndex)] : 0u;
                if (sameKind && from + p.size <= authored.uniformDefaults.Size())
                {
                    MemCopy(out.uniformDefaults.Data() + p.offset,
                            authored.uniformDefaults.Data() + from, p.size);
                    continue;
                }
                WriteDefault(p, out.uniformDefaults.Data() + p.offset);
            }

            for (usize i = 0; i < authored.propertyNames.Size(); ++i)
            {
                bool kept = false;
                for (const sh::ShaderProperty& p : table)
                {
                    kept = kept || p.name == authored.propertyNames[i];
                }
                if (!kept)
                {
                    LOG_WARNING(u8"Materials",
                                u8"material '{}': '{}' is not a property of shader '{}' - dropped",
                                authored.name, authored.propertyNames[i], shader.name);
                }
            }
            for (usize i = 0; i < authored.textureSlots.Size(); ++i)
            {
                if (IndexOf(out.textureSlots, authored.textureSlots[i].AsView()) < 0)
                {
                    LOG_WARNING(u8"Materials",
                                u8"material '{}': shader '{}' has no texture '{}' - its binding "
                                u8"is dropped",
                                authored.name, shader.name, authored.textureSlots[i]);
                }
            }
        }

    private:
        [[nodiscard]] static MaterialPropertyType MaterialTypeOf(
            foundation::shaders::ShaderPropertyType type) noexcept
        {
            using T = foundation::shaders::ShaderPropertyType;
            switch (type)
            {
            case T::Float: return MaterialPropertyType::Float;
            case T::Float2: return MaterialPropertyType::Float2;
            case T::Float3: return MaterialPropertyType::Float3;
            case T::Float4: return MaterialPropertyType::Float4;
            case T::Int: return MaterialPropertyType::Int;
            case T::Color: return MaterialPropertyType::Color;
            case T::ColorHdr: return MaterialPropertyType::ColorHdr;
            case T::Texture: return MaterialPropertyType::Texture2D;
            case T::TextureCube: return MaterialPropertyType::TextureCube;
            case T::Sampler: return MaterialPropertyType::Sampler;
            }
            return MaterialPropertyType::Float;
        }

        [[nodiscard]] static isize IndexOf(const Array<String>& names, StringView name) noexcept
        {
            for (usize i = 0; i < names.Size(); ++i)
            {
                if (names[i].AsView() == name)
                {
                    return static_cast<isize>(i);
                }
            }
            return -1;
        }

        // The shader's default for a uniform, written as the shader reads it (an int as an int).
        static void WriteDefault(const foundation::shaders::ShaderProperty& p, u8* at) noexcept
        {
            if (p.type == foundation::shaders::ShaderPropertyType::Int)
            {
                const i32 value = static_cast<i32>(p.value.x);
                MemCopy(at, &value, sizeof(value));
                return;
            }
            const f32 values[4] = {p.value.x, p.value.y, p.value.z, p.value.w};
            MemCopy(at, values, p.size);
        }
    };

    // Authoring helper: capture a Material (built in code) + its shader id into an asset.
    class MaterialImporter
    {
    public:
        static void Import(const Material& material, const Guid& shaderId, MaterialAsset& outAsset)
        {
            MaterialSource::FromMaterial(material, shaderId, outAsset.source);
        }
    };

    // Registers MaterialAsset for content-DB construction + deserialization.
    inline void RegisterMaterialAsset()
    {
        RegisterMaterialsTypeReflection(); // the render-state enums the source's fields resolve to
        GlobalTypeRegistry().Register(MaterialAsset::StaticType(), TypeDomain(u8"Pipeline"));
        RegisterSerializable<MaterialAsset>();
    }

    // MaterialAsset::StaticType() is defined WITH its reflected surface (a Nested `source`
    // property) in MaterialAssetImpl.cpp - GCC module hygiene: REFLECT_MEMBERS out of interfaces.


    /// A preset material (PBR, or unlit) in `target`, named `baseName` made unique.
    [[nodiscard]] foundation::content::Instance* CreateMaterialInstance(
        foundation::content::Group* target, StringView baseName, bool unlit);
    /// File > New's material creators (pipeline.registration composes every domain's).
    void RegisterMaterialCreators(AssetCreatorRegistry& registry);
} // namespace foundation::materials
