// Raptor::MaterialEditor — the `raptor.materials.editor` module (tooling).
//
// Source-side material authoring + cook:
//   * MaterialAsset (editor::Asset): wraps a MaterialSource (the authored material —
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

export module raptor.materials.editor;

import raptor.core;
import raptor.editor;
import raptor.content;
import raptor.materials;
import raptor.materials.resource;

using namespace raptor::core;

export namespace raptor::materials {

// Source asset wrapping the authored material data.
class MaterialAsset final : public raptor::editor::Asset {
    RAPTOR_OBJECT(MaterialAsset, raptor::editor::Asset)
public:
    MaterialSource source;

    void Serialize(ISerializer& ar) override {
        raptor::editor::Asset::Serialize(ar);   // fileName (optional authoring note)
        source.Serialize(ar);
    }
};

// Cooks a MaterialAsset -> MaterialSource in the output DB (the authored source is
// already the runtime source; writing it is the cook).
class MaterialAssetBuilder final : public raptor::editor::DefaultAssetBuilder {
public:
    [[nodiscard]] const TypeInfo* AssetType() const override { return &MaterialAsset::StaticType(); }

    [[nodiscard]] Status Build(const raptor::editor::Asset& asset, raptor::editor::AssetBuildContext& ctx) override {
        const MaterialAsset& ma = static_cast<const MaterialAsset&>(asset);   // guarded by AssetType()
        if (ctx.output == nullptr) { return Status{ ErrorCode::InvalidArgument }; }
        // WriteObject only reads during a write pass; const_cast is safe here.
        return ctx.output->WriteObject(const_cast<MaterialSource&>(ma.source));
    }
};

// Authoring helper: capture a Material (built in code) + its shader id into an asset.
class MaterialImporter {
public:
    static void Import(const Material& material, const Guid& shaderId, MaterialAsset& outAsset) {
        MaterialSource::FromMaterial(material, shaderId, outAsset.source);
    }
};

// Registers MaterialAsset for content-DB construction + deserialization.
inline void RegisterMaterialAsset() {
    GlobalTypeRegistry().Register(MaterialAsset::StaticType());
    RegisterSerializable<MaterialAsset>();
}

RAPTOR_DEFINE_OBJECT(MaterialAsset, "raptor::materials")

} // namespace raptor::materials
