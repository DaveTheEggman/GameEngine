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
import pipeline.importer; // IFileImporter + ImportContext + CopyIntoSources (splatmap PNG import)
import foundation.image;
import foundation.image.io; // LoadImageFromMemory (RGBA8 decode - reuses the image decoder)
import foundation.terrain.resource;
import foundation.content;

using namespace foundation::core;
namespace content = foundation::content;

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

        // An EMBEDDED (create + paint) splatmap reads the "pixels" source stream - declare it so the
        // recipe hash chains its bytes (the envelope hash does not cover sidecars). An IMPORTED one
        // (fileName set) chains the file itself, which the base builder already tracks.
        void ScanDependencies(const pipeline::Asset& asset, pipeline::AssetBuildContext&,
                              pipeline::AssetDependencies& out) override
        {
            const SplatmapAsset& sa = static_cast<const SplatmapAsset&>(asset);
            if (sa.fileName.IsEmpty())
            {
                out.sourceStreams.PushBack(String(foundation::terrain::kSplatStream));
            }
        }

        [[nodiscard]] Status Build(const pipeline::Asset& asset,
                                   pipeline::AssetBuildContext& ctx) override
        {
            const SplatmapAsset& sa = static_cast<const SplatmapAsset&>(asset); // AssetType()-guarded
            if (ctx.output == nullptr)
            {
                return Status{ErrorCode::InvalidArgument};
            }

            RefPtr<Splatmap> sm;
            if (!sa.fileName.View().IsEmpty())
            {
                // IMPORTED: decode the source image (PNG etc.) as RGBA8 at its native size - the same
                // image decoder HeightfieldAsset uses for heightmaps. A splatmap is arbitrary WxH
                // (no 64k+1 rule), so no resampling; the runtime product stays a versioned Splatmap.
                Result<Array<byte>> bytes = ReadSourceBytes(ctx, sa.fileName.View());
                if (!bytes.HasValue())
                {
                    return Status{bytes.Error()};
                }
                foundation::image::Image img;
                const Status loaded = foundation::image::io::LoadImageFromMemory(
                    Span<const u8>(reinterpret_cast<const u8*>(bytes.Value().Data()),
                                   bytes.Value().Size()),
                    img);
                if (!loaded.IsOk())
                {
                    return loaded;
                }
                if (img.Format() != foundation::image::PixelFormat::RGBA8)
                {
                    return Status{ErrorCode::NotSupported}; // HDR/other - splatmaps are RGBA8 weights
                }
                sm = MakeRef<Splatmap>(DefaultAllocator(), static_cast<i32>(img.Width()),
                                       static_cast<i32>(img.Height()));
                const Span<const u8> px = img.PixelData();
                const usize n = Min(px.Size(), sm->Pixels().Size());
                if (n > 0)
                {
                    MemCopy(sm->Pixels().Data(), px.Data(), n);
                }
            }
            else
            {
                // EMBEDDED (create + paint): the pixels ride the source instance's "pixels" sidecar.
                const i32 w = sa.width > 0 ? sa.width : 1;
                const i32 h = sa.height > 0 ? sa.height : 1;
                sm = MakeRef<Splatmap>(DefaultAllocator(), w, h);
                bool havePixels = false;
                if (ctx.source != nullptr)
                {
                    if (UniquePtr<IStream> stream =
                            ctx.source->ReadData(foundation::terrain::kSplatStream))
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

    // OS-file importer (editor drag-drop): imports a PNG (or any stb-decodable image) as a
    // SplatmapAsset with fileName set - the builder decodes it to the RGBA8 weight raster. Reuses
    // the image decoder, NOT a TextureAsset/ImageAsset reference (the terrain needs a Splatmap
    // product). The layer semantics (R/G/B/A = layers 0..3) are the author's responsibility.
    class SplatmapFileImporter final : public pipeline::IFileImporter
    {
    public:
        [[nodiscard]] StringView Label() const override { return u8"Splatmap"; }

        [[nodiscard]] bool Accepts(StringView extension) const override
        {
            return extension == u8"png";
        }

        [[nodiscard]] Result<content::Instance*>
        Import(StringView sourcePath, const pipeline::ImportContext& context, content::Group& group,
               const pipeline::ImportOptions*, Object*, Array<pipeline::DeferredImportWrite>*) override
        {
            Result<String> fileName = pipeline::CopyIntoSources(context, sourcePath);
            if (!fileName.HasValue())
            {
                return Err(fileName.Error());
            }
            const StringView stem = pipeline::FileStemOf(fileName.Value().AsView());
            content::Instance* instance = group.CreateInstance(stem, SplatmapAsset::StaticType());
            if (instance == nullptr)
            {
                return Err(ErrorCode::Unknown);
            }
            SplatmapAsset asset;
            asset.fileName = foundation::vfs::SourcePath(fileName.Value().AsView());
            const Status written = instance->WriteObject(asset);
            if (!written.IsOk())
            {
                return Err(written.Code());
            }
            return instance;
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
