// draconic.particles.resource - the cooked ParticleEffectResource (runtime input), its serializer,
// and its resource factory. A ParticleEffectResource IS a reflected ISerializable that holds a
// runtime ParticleEffect; the cook (draconic.particles.editor) writes one into the content DB, the
// factory reconstructs it at Bind. Polymorphic modules round-trip via the reflection/serializable
// registry (Serializables().Create by type-id) - the same machinery TextureResource uses.
//
// See docs/design/particles-authoring.md.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include <utility>   // std::move

export module draconic.particles.resource;

import draconic.core;
import draconic.particles;
import draconic.content;
import draconic.resource;

using namespace draconic::core;
namespace content = draconic::content;
namespace resource = draconic::resource;

namespace draconic::particles
{
    // ---- Effect serializer (bidirectional; ported from Sedulous ParticleEffectSerializer) --------

    // A polymorphic module: write its reflected type-id (u64) + params; on read, reconstruct via the
    // serializable registry, read its params, and add it to the system (which declares its streams).
    inline void SerializeInitializers(ISerializer& ar, ParticleSystem& sys)
    {
        const bool reading = ar.Mode() == SerializeMode::Read;
        u32 count = reading ? 0u : static_cast<u32>(sys.InitializerCount());
        ar.Key("initializers"); ar.BeginArray(count);
        for (u32 i = 0; i < count; ++i)
        {
            ar.BeginObject();
            ParticleInitializer* mod = reading ? nullptr : sys.GetInitializer(static_cast<i32>(i));
            u64 typeId = reading ? 0ull : mod->GetType()->id;
            core::Serialize(ar, "type", typeId);
            RefPtr<ParticleInitializer> created;
            if (reading)
            {
                RefPtr<ISerializable> obj = GlobalSerializableRegistry().Create(typeId);
                if (obj) { created = RefPtr<ParticleInitializer>{ Cast<ParticleInitializer>(obj.Get()) }; mod = created.Get(); }
            }
            if (mod != nullptr) { mod->Serialize(ar); }
            ar.EndObject();
            if (reading && created) { sys.AddInitializer(std::move(created)); }
        }
        ar.EndArray();
    }

    inline void SerializeBehaviors(ISerializer& ar, ParticleSystem& sys)
    {
        const bool reading = ar.Mode() == SerializeMode::Read;
        u32 count = reading ? 0u : static_cast<u32>(sys.BehaviorCount());
        ar.Key("behaviors"); ar.BeginArray(count);
        for (u32 i = 0; i < count; ++i)
        {
            ar.BeginObject();
            ParticleBehavior* mod = reading ? nullptr : sys.GetBehavior(static_cast<i32>(i));
            u64 typeId = reading ? 0ull : mod->GetType()->id;
            core::Serialize(ar, "type", typeId);
            RefPtr<ParticleBehavior> created;
            if (reading)
            {
                RefPtr<ISerializable> obj = GlobalSerializableRegistry().Create(typeId);
                if (obj) { created = RefPtr<ParticleBehavior>{ Cast<ParticleBehavior>(obj.Get()) }; mod = created.Get(); }
            }
            if (mod != nullptr) { mod->Serialize(ar); }
            ar.EndObject();
            if (reading && created) { sys.AddBehavior(std::move(created)); }
        }
        ar.EndArray();
    }

    inline void SerializeSystem(ISerializer& ar, ParticleEffect& fx, i32 index)
    {
        const bool reading = ar.Mode() == SerializeMode::Read;
        i32 maxParticles = reading ? 0 : fx.GetSystem(index)->MaxParticles();
        u64 seed = reading ? 0ull : fx.GetSystem(index)->Seed();
        core::Serialize(ar, "maxParticles", maxParticles);
        core::Serialize(ar, "seed", seed);
        ParticleSystem* sys = reading ? &fx.AddSystem(maxParticles, seed) : fx.GetSystem(index);

        core::Serialize(ar, "name", sys->name);
        core::Serialize(ar, "desiredMode", sys->desiredMode);
        core::Serialize(ar, "simSpace", sys->simulationSpace);
        core::Serialize(ar, "blend", sys->blendMode);
        core::Serialize(ar, "render", sys->renderMode);
        core::Serialize(ar, "sort", sys->sortParticles);
        core::Serialize(ar, "soft", sys->softParticles);
        core::Serialize(ar, "softDistance", sys->softDistance);
        core::Serialize(ar, "trail", sys->trail);
        core::Serialize(ar, "flipbook", sys->flipbook);
        core::Serialize(ar, "prewarm", sys->prewarmTime);
        core::Serialize(ar, "lodStart", sys->lodStartDistance);
        core::Serialize(ar, "lodCull", sys->lodCullDistance);
        core::Serialize(ar, "lodMinRate", sys->lodMinRate);

        ar.Key("emitter"); ar.BeginObject();
        core::Serialize(ar, "mode", sys->emitter.mode);
        core::Serialize(ar, "spawnRate", sys->emitter.spawnRate);
        core::Serialize(ar, "burstCount", sys->emitter.burstCount);
        core::Serialize(ar, "burstInterval", sys->emitter.burstInterval);
        core::Serialize(ar, "burstCycles", sys->emitter.burstCycles);
        core::Serialize(ar, "isEmitting", sys->emitter.isEmitting);
        core::Serialize(ar, "duration", sys->emitter.duration);
        core::Serialize(ar, "looping", sys->emitter.looping);
        ar.EndObject();

        SerializeInitializers(ar, *sys);
        SerializeBehaviors(ar, *sys);
    }

    inline void SerializeEffect(ISerializer& ar, ParticleEffect& fx)
    {
        const bool reading = ar.Mode() == SerializeMode::Read;
        core::Serialize(ar, "name", fx.name);

        u32 systemCount = reading ? 0u : static_cast<u32>(fx.SystemCount());
        ar.Key("systems"); ar.BeginArray(systemCount);
        for (u32 i = 0; i < systemCount; ++i) { ar.BeginObject(); SerializeSystem(ar, fx, static_cast<i32>(i)); ar.EndObject(); }
        ar.EndArray();

        const Span<const SubEmitterLink> links = fx.SubEmitterLinks();
        u32 linkCount = reading ? 0u : static_cast<u32>(links.Size());
        ar.Key("links"); ar.BeginArray(linkCount);
        for (u32 i = 0; i < linkCount; ++i)
        {
            ar.BeginObject();
            SubEmitterLink link = reading ? SubEmitterLink{} : links[static_cast<usize>(i)];
            Serialize(ar, link);
            ar.EndObject();
            if (reading) { fx.AddSubEmitterLink(link); }
        }
        ar.EndArray();
    }
}

export namespace draconic::particles
{
    // ---- Cooked resource ---------------------------------------------------------------------
    // Both the cooked record AND the runtime product (no GPU transform needed): holds a template
    // ParticleEffect. A component instantiates its own ParticleEffectInstance over this effect.
    class ParticleEffectResource final : public ISerializable
    {
        DRACONIC_OBJECT(ParticleEffectResource, ISerializable)
    public:
        [[nodiscard]] ParticleEffect& Effect() noexcept { return m_effect; }
        [[nodiscard]] const ParticleEffect& Effect() const noexcept { return m_effect; }
        void Serialize(ISerializer& ar) override { SerializeEffect(ar, m_effect); }
    private:
        ParticleEffect m_effect;
    };

    // ---- Factory -----------------------------------------------------------------------------
    // Data factory (model B): deserialize the record; no GPU upload. Referenced cooked resources
    // (textures/meshes/materials), when added, are attached here via manager.Bind<T> (dependency edges).
    class ParticleEffectFactory final : public resource::IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override { return &ParticleEffectResource::StaticType(); }
        [[nodiscard]] RefPtr<Object> Create(resource::ResourceManager& manager, content::Instance& instance) override
        {
            (void)manager;
            return instance.ReadObject();   // ParticleEffectResource (ISerializable -> Object)
        }
    };

    // Register the cooked resource type + all module types. Call once at startup (tooling and runtime).
    inline void RegisterParticleEffectResource()
    {
        RegisterParticleModules();
        GlobalTypeRegistry().Register(ParticleEffectResource::StaticType());
        RegisterSerializable<ParticleEffectResource>();
    }

    DRACONIC_DEFINE_OBJECT(ParticleEffectResource, "draconic::particles")
}
