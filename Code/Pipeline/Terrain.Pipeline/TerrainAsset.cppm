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

    using foundation::terrain::Splatmap;
    using foundation::terrain::SplatmapSource;

    // Source asset: an editable RGBA8 splatmap of a given size. v1 authoring is CREATE + PAINT (the
    // TerrainPage seeds one, the Splat Paint tool writes it), so there is no file/import - the pixels
    // live in the source instance's "pixels" data stream (the embedded-TextureAsset precedent).
    // Import-from-PNG is deferred.
    class SplatmapAsset final : public pipeline::Asset
    {
        RTTI_OBJECT(SplatmapAsset, pipeline::Asset)
    public:
        i32 width = 1024;  // weight-raster resolution (authoring choice, independent of the heightfield)
        i32 height = 1024;

        void Serialize(ISerializer& ar) override
        {
            pipeline::Asset::Serialize(ar); // fileName (unused in v1)
            foundation::core::Serialize(ar, "width", width);
            foundation::core::Serialize(ar, "height", height);
        }
    };

    // Cooks a SplatmapAsset -> Splatmap resource (metadata object + the "pixels" RGBA8 stream). The
    // pixels pass through from the source instance's "pixels" sidecar; a source with none cooks a
    // layer-0-seeded raster (a freshly created, never-painted splatmap still renders the base layer).
    class SplatmapAssetBuilder final : public pipeline::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &SplatmapAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &Splatmap::StaticType();
        }

        // Declare the "pixels" source stream so the recipe hash chains its bytes (the envelope hash
        // does not cover sidecars - editing the painted pixels must re-cook).
        void ScanDependencies(const pipeline::Asset& asset, pipeline::AssetBuildContext&,
                              pipeline::AssetDependencies& out) override
        {
            (void)asset;
            out.sourceStreams.PushBack(String(foundation::terrain::kSplatStream));
        }

        [[nodiscard]] Status Build(const pipeline::Asset& asset,
                                   pipeline::AssetBuildContext& ctx) override
        {
            const SplatmapAsset& sa = static_cast<const SplatmapAsset&>(asset); // AssetType()-guarded
            if (ctx.output == nullptr)
            {
                return Status{ErrorCode::InvalidArgument};
            }
            const i32 w = sa.width > 0 ? sa.width : 1;
            const i32 h = sa.height > 0 ? sa.height : 1;
            RefPtr<Splatmap> sm = MakeRef<Splatmap>(DefaultAllocator(), w, h);

            bool havePixels = false;
            if (ctx.source != nullptr)
            {
                if (UniquePtr<IStream> stream = ctx.source->ReadData(foundation::terrain::kSplatStream))
                {
                    const i64 size = stream->Size();
                    const i64 expected = static_cast<i64>(w) * static_cast<i64>(h) * 4;
                    if (size == expected &&
                        stream->Read(sm->Pixels().Data(), static_cast<u64>(size)) ==
                            static_cast<u64>(size))
                    {
                        havePixels = true;
                    }
                }
            }
            if (!havePixels)
            {
                sm->SeedLayer0(); // never painted yet: cook a valid base-layer raster
            }

            SplatmapSource src;
            SplatmapSource::FromSplatmap(*sm, src);
            const Status wrote = ctx.output->WriteObject(src);
            if (!wrote.IsOk())
            {
                return wrote;
            }
            return ctx.output->WriteData(foundation::terrain::kSplatStream,
                                         SplatmapSource::PixelBlob(*sm));
        }
    };

    // Registers SplatmapAsset for content-DB construction + deserialization (reflection body in
    // TerrainAssetImpl.cpp per GCC module hygiene).
    inline void RegisterSplatmapAsset()
    {
        GlobalTypeRegistry().Register(SplatmapAsset::StaticType(), TypeDomain(u8"Pipeline"));
        RegisterSerializable<SplatmapAsset>();
    }
}
