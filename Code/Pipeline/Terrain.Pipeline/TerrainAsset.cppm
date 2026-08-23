// Pipeline::Terrain - the `terrain.pipeline` module.
//
// Tooling: the source TerrainAsset (references a heightfield asset + a splatmap + per-layer albedo
// textures + tiling + cast-shadows) and the builder that cooks it into the Terrain resource. It does
// NOT import a heightmap - that is Heightfield.Pipeline; a terrain REFERENCES an existing heightfield
// asset. The cook is a reference pass-through (asset ids == cooked product ids). Never linked by the
// runtime.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module terrain.pipeline;

import foundation.core;
import foundation.vfs;
import pipeline.core;
import foundation.terrain.resource;
import foundation.content;

using namespace foundation::core;

export namespace pipeline
{
    using foundation::terrain::TerrainResource;
    using foundation::terrain::TerrainSource;

    // Source asset: references a heightfield + splatmap + per-layer albedo textures (by asset guid,
    // which equal their cooked product guids) + per-layer tiling + a cast-shadows flag.
    class TerrainAsset final : public pipeline::Asset
    {
        RTTI_OBJECT(TerrainAsset, pipeline::Asset)
    public:
        Guid heightfieldId;         // the referenced heightfield asset (shared with physics/nav)
        Guid splatmapId;            // one RGBA splatmap (up to 4 layers in P1); nil = none
        Array<Guid> layerAlbedoIds; // per-layer albedo texture (parallel to layerTileScales)
        Array<f32> layerTileScales; // per-layer UV tiling
        bool castShadows = true;

        void Serialize(ISerializer& ar) override
        {
            pipeline::Asset::Serialize(ar); // fileName (unused; terrain references sub-assets)
            foundation::core::Serialize(ar, "heightfieldId", heightfieldId);
            foundation::core::Serialize(ar, "splatmapId", splatmapId);
            foundation::core::Serialize(ar, "layerAlbedoIds", layerAlbedoIds);
            foundation::core::Serialize(ar, "layerTileScales", layerTileScales);
            foundation::core::Serialize(ar, "castShadows", castShadows);
        }
    };

    // Cooks a TerrainAsset -> Terrain resource (a reference pass-through; the referenced products are
    // resolved at load by TerrainFactory).
    class TerrainAssetBuilder final : public pipeline::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &TerrainAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &TerrainResource::StaticType();
        }

        [[nodiscard]] Status Build(const pipeline::Asset& asset,
                                   pipeline::AssetBuildContext& ctx) override
        {
            const TerrainAsset& ta = static_cast<const TerrainAsset&>(asset); // AssetType()-guarded
            if (ctx.output == nullptr)
            {
                return Status{ErrorCode::InvalidArgument};
            }
            TerrainSource src;
            src.heightfieldId = ta.heightfieldId;
            src.splatmapId = ta.splatmapId;
            for (usize i = 0; i < ta.layerAlbedoIds.Size(); ++i)
            {
                src.layerAlbedoIds.PushBack(ta.layerAlbedoIds[i]);
            }
            for (usize i = 0; i < ta.layerTileScales.Size(); ++i)
            {
                src.layerTileScales.PushBack(ta.layerTileScales[i]);
            }
            src.castShadows = ta.castShadows;
            return ctx.output->WriteObject(src);
        }
    };

    // Registers TerrainAsset for content-DB construction + deserialization (reflection body in
    // TerrainAssetImpl.cpp per GCC module hygiene).
    inline void RegisterTerrainAsset()
    {
        GlobalTypeRegistry().Register(TerrainAsset::StaticType(), TypeDomain(u8"Pipeline"));
        RegisterSerializable<TerrainAsset>();
    }
}
