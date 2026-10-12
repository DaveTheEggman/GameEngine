// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Foundation::Shaders.Resource - the `foundation.shaders.resource` module.
///
/// Shaders as resources: a `ShaderSource` (authored content: name + per-stage HLSL)
/// is built by `ShaderFactory` into a runtime `ShaderResource`. The factory
/// registers the sources with the ShaderSystem and bumps the shader's version, so
/// a reload (via the resource manager) propagates: PSO caches see the version
/// change and rebuild, and a material that depends on this ShaderResource is a
/// normal resource→resource edge. Variants themselves live in the (shared)
/// ShaderSystem; this product is the resource-system handle + version accessor.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.shaders.resource;

import foundation.core;
import foundation.rhi;
import foundation.shaders;
import foundation.shaders.system;
import foundation.resource;
import foundation.content;

using namespace foundation::core;
using namespace foundation::resource;
namespace rhi = foundation::rhi;

export namespace foundation::shaders
{

    // A cooked shader: a name + per-stage HLSL (the cook's expanded source: a properties block
    // already replaced by its set-2 declarations), the entry point each stage's function is called
    // in it, each stage's declared variant lattice, and the material property table the properties
    // block declared (the layout every material on this shader is cooked with).
    class ShaderSource final : public ISerializable
    {
        RTTI_OBJECT(ShaderSource, ISerializable)
    public:
        String name;
        String vertexSource;
        String fragmentSource;
        String vertexEntryPoint;
        String fragmentEntryPoint;
        u32 vertexVariantMask = 0;   // ShaderFlags the vertex stage declares
        u32 fragmentVariantMask = 0; // ShaderFlags the fragment stage declares
        // The property table, as parallel arrays (the primitive Array<T> serialization path).
        Array<String> propertyNames;
        Array<u8> propertyTypes;     // ShaderPropertyType
        Array<f32> propertyDefaults; // 4 per property
        Array<u8> propertyHasRange;
        Array<f32> propertyRanges;   // min, max per property
        Array<u32> propertyOffsets;
        Array<u32> propertySizes;
        Array<u32> propertyBindings;
        u32 uniformSize = 0;

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "name", name);
            foundation::core::Serialize(ar, "vertexSource", vertexSource);
            foundation::core::Serialize(ar, "fragmentSource", fragmentSource);
            foundation::core::Serialize(ar, "vertexEntryPoint", vertexEntryPoint);
            foundation::core::Serialize(ar, "fragmentEntryPoint", fragmentEntryPoint);
            foundation::core::Serialize(ar, "vertexVariantMask", vertexVariantMask);
            foundation::core::Serialize(ar, "fragmentVariantMask", fragmentVariantMask);
            foundation::core::Serialize(ar, "propertyNames", propertyNames);
            foundation::core::Serialize(ar, "propertyTypes", propertyTypes);
            foundation::core::Serialize(ar, "propertyDefaults", propertyDefaults);
            foundation::core::Serialize(ar, "propertyHasRange", propertyHasRange);
            foundation::core::Serialize(ar, "propertyRanges", propertyRanges);
            foundation::core::Serialize(ar, "propertyOffsets", propertyOffsets);
            foundation::core::Serialize(ar, "propertySizes", propertySizes);
            foundation::core::Serialize(ar, "propertyBindings", propertyBindings);
            foundation::core::Serialize(ar, "uniformSize", uniformSize);
        }

        // Records a parsed properties block's table.
        void SetProperties(const ShaderPropertiesBlock& block)
        {
            propertyNames.Clear();
            propertyTypes.Clear();
            propertyDefaults.Clear();
            propertyHasRange.Clear();
            propertyRanges.Clear();
            propertyOffsets.Clear();
            propertySizes.Clear();
            propertyBindings.Clear();
            for (const ShaderProperty& p : block.properties)
            {
                propertyNames.PushBack(p.name);
                propertyTypes.PushBack(static_cast<u8>(p.type));
                propertyDefaults.PushBack(p.value.x);
                propertyDefaults.PushBack(p.value.y);
                propertyDefaults.PushBack(p.value.z);
                propertyDefaults.PushBack(p.value.w);
                propertyHasRange.PushBack(p.hasRange ? 1u : 0u);
                propertyRanges.PushBack(p.rangeMin);
                propertyRanges.PushBack(p.rangeMax);
                propertyOffsets.PushBack(p.offset);
                propertySizes.PushBack(p.size);
                propertyBindings.PushBack(p.binding);
            }
            uniformSize = block.uniformSize;
        }

        // The property table, as SetProperties recorded it. A table whose arrays disagree in
        // length (a damaged product) yields what the shortest covers.
        [[nodiscard]] Array<ShaderProperty> Properties() const
        {
            Array<ShaderProperty> out;
            usize count = propertyNames.Size();
            count = Min(count, propertyTypes.Size());
            count = Min(count, propertyDefaults.Size() / 4u);
            count = Min(count, propertyHasRange.Size());
            count = Min(count, propertyRanges.Size() / 2u);
            count = Min(count, Min(propertyOffsets.Size(), Min(propertySizes.Size(),
                                                               propertyBindings.Size())));
            for (usize i = 0; i < count; ++i)
            {
                ShaderProperty p;
                p.name = propertyNames[i];
                p.type = static_cast<ShaderPropertyType>(propertyTypes[i]);
                p.value = Float4{propertyDefaults[i * 4], propertyDefaults[i * 4 + 1],
                                 propertyDefaults[i * 4 + 2], propertyDefaults[i * 4 + 3]};
                p.hasRange = propertyHasRange[i] != 0u;
                p.rangeMin = propertyRanges[i * 2];
                p.rangeMax = propertyRanges[i * 2 + 1];
                p.offset = propertyOffsets[i];
                p.size = propertySizes[i];
                p.binding = propertyBindings[i];
                out.PushBack(Move(p));
            }
            return out;
        }
    };

    // Runtime handle for a shader: its name + the ShaderSystem it lives in. Version()
    // is the reload signal (bumped each rebuild); GetVariant compiles-on-demand.
    class ShaderResource final : public Object
    {
        RTTI_OBJECT(ShaderResource, Object)
    public:
        void Init(ShaderSystem* system, StringView name)
        {
            m_system = system;
            m_name = String(name);
        }

        [[nodiscard]] StringView Name() const noexcept { return m_name.AsView(); }
        [[nodiscard]] u64 Version()
        {
            return (m_system != nullptr) ? m_system->Version(m_name.AsView()) : 0ull;
        }
        [[nodiscard]] rhi::ShaderModule* GetVariant(ShaderStage stage, ShaderFlags flags)
        {
            return (m_system != nullptr) ? m_system->GetVariant(m_name.AsView(), stage, flags)
                                         : nullptr;
        }

    private:
        ShaderSystem* m_system = nullptr; // borrowed
        String m_name;
    };

    // Builds a ShaderResource from a ShaderSource: registers the sources with the
    // ShaderSystem and invalidates (clears stale variants + bumps version) so a reload
    // is observable. Constructed with the ShaderSystem the shaders live in.
    class ShaderFactory final : public IResourceFactory
    {
    public:
        // The allocator backs every product this factory creates (required - the composition
        // that creates the factory decides); the shader system is borrowed from the host.
        ShaderFactory(IAllocator& allocator, ShaderSystem& system) noexcept
            : m_allocator(&allocator), m_system(&system)
        {
        }

        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &ShaderResource::StaticType();
        }
        [[nodiscard]] const TypeInfo* CookedType() const override
        {
            return &ShaderSource::StaticType();
        }

        [[nodiscard]] RefPtr<Object> Create(ResourceManager& manager,
                                            foundation::content::Instance& instance) override
        {
            (void)manager;
            RefPtr<ISerializable> object = instance.ReadObject();
            ShaderSource* source = Cast<ShaderSource>(object.Get());
            if (source == nullptr)
            {
                return RefPtr<Object>{};
            }

            // Each stage with its entry point and the variant lattice the cook compiled, so a
            // request canonicalizes onto a variant the cook proved (as the corpus's do).
            m_system->RegisterSource(source->name.AsView(), ShaderStage::Vertex,
                                     source->vertexSource.AsView(),
                                     source->vertexEntryPoint.AsView(),
                                     static_cast<ShaderFlags>(source->vertexVariantMask));
            m_system->RegisterSource(source->name.AsView(), ShaderStage::Fragment,
                                     source->fragmentSource.AsView(),
                                     source->fragmentEntryPoint.AsView(),
                                     static_cast<ShaderFlags>(source->fragmentVariantMask));
            m_system->InvalidateShader(source->name.AsView()); // drop stale variants + bump version

            RefPtr<ShaderResource> res = MakeRef<ShaderResource>(*m_allocator);
            res->Init(m_system, source->name.AsView());
            return res;
        }

    private:
        IAllocator* m_allocator;
        ShaderSystem* m_system; // borrowed
    };

    RTTI_DEFINE_OBJECT(ShaderSource, "rtti::shaders")
    RTTI_DEFINE_OBJECT(ShaderResource, "rtti::shaders")

} // namespace foundation::shaders

export namespace foundation::shaders
{
    /// Registers the cooked record + product (content-DB construction by type name). Idempotent.
    inline void RegisterShaderResourceTypes()
    {
        GlobalTypeRegistry().Register(ShaderSource::StaticType());
        RegisterSerializable<ShaderSource>();
        GlobalTypeRegistry().Register(ShaderResource::StaticType());
    }

    /// The shaders resource module (engine-composition.md D1): the module the engine
    /// composition composes this library's factories from.
    inline constexpr foundation::resource::ResourceFactoryDesc kShadersResourceFactories[] = {
        foundation::resource::FactoryWithService<ShaderResource, ShaderSource, ShaderFactory, ShaderSystem>(),
    };
    inline constexpr foundation::resource::ResourceModule kShadersResourceModule{
        u8"shaders", &RegisterShaderResourceTypes, kShadersResourceFactories,
        sizeof(kShadersResourceFactories) / sizeof(kShadersResourceFactories[0])};
}
