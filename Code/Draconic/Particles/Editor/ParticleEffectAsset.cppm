// draconic.particles.editor - the edit-time ParticleEffectAsset + its builder (the bake). Tooling
// only; the runtime/app never links this. Unlike an imported asset (e.g. a texture importing an
// external .png), a particle effect is AUTHORED - so the asset embeds the effect itself and Build()
// cooks it into a ParticleEffectResource with no source-file load. Ref resolution + curve->LUT baking
// are later transforms; v1 is a straight pass-through of the authored effect.
//
// See docs/design/particles-authoring.md.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.particles.editor;

import draconic.core;
import draconic.editor;
import draconic.particles;
import draconic.particles.resource;
import draconic.content;

using namespace draconic::core;

export namespace draconic::particles
{
    // Edit-time asset: the authored effect (embedded, since there is no external source file - unlike an
    // imported texture). Serialized to a readable .particlefx (XmlSerializer) as fileName + the effect
    // graph. This is a distinct type from the cooked ParticleEffectResource; Build() transforms one into
    // the other.
    class ParticleEffectAsset final : public draconic::editor::Asset
    {
        DRACONIC_OBJECT(ParticleEffectAsset, draconic::editor::Asset)
    public:
        [[nodiscard]] ParticleEffect& Effect() noexcept { return m_effect; }
        [[nodiscard]] const ParticleEffect& Effect() const noexcept { return m_effect; }

        void Serialize(ISerializer& ar) override
        {
            draconic::editor::Asset::Serialize(ar);   // fileName (unused for authored effects)
            SerializeEffect(ar, m_effect);            // the authored effect graph
        }

    private:
        ParticleEffect m_effect;
    };

    // The bake: cook a ParticleEffectAsset into the output content Instance as a ParticleEffectResource.
    class ParticleEffectAssetBuilder final : public draconic::editor::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override { return &ParticleEffectAsset::StaticType(); }
        [[nodiscard]] Status Build(const draconic::editor::Asset& asset, draconic::editor::AssetBuildContext& ctx) override
        {
            if (ctx.output == nullptr) { return Status{ ErrorCode::InvalidArgument }; }
            const ParticleEffectAsset& pa = static_cast<const ParticleEffectAsset&>(asset);
            // Bake: build a fresh cooked resource from the authored effect. v1 is a faithful deep copy;
            // this is where later transforms slot in - resolve texture/mesh/material refs to cooked GUIDs
            // and (optionally) bake curves -> LUTs.
            ParticleEffectResource cooked;
            CloneEffect(pa.Effect(), cooked.Effect());
            return ctx.output->WriteObject(cooked);
        }
    };

    // Register the asset type (+ the cooked resource + modules it depends on). Tooling-side.
    inline void RegisterParticleEffectAsset()
    {
        RegisterParticleEffectResource();
        GlobalTypeRegistry().Register(ParticleEffectAsset::StaticType());
        RegisterSerializable<ParticleEffectAsset>();
    }

    DRACONIC_DEFINE_OBJECT(ParticleEffectAsset, "draconic::particles")
}
