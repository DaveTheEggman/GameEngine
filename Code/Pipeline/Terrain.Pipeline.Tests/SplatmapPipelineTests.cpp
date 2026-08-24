// SplatmapAsset cook round-trip: author an RGBA8 splatmap (embedded "pixels" sidecar), cook it with
// SplatmapAssetBuilder into a Splatmap product, load it back through SplatmapFactory and confirm the
// raster is byte-identical and the PRODUCT guid == the SOURCE guid (the ref-id parity the whole
// splat-in-terrain resolution rests on - the same invariant the heightfield/model tests pin).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import pipeline.core;
import foundation.image;
import foundation.image.io;
import foundation.terrain.resource;
import terrain.pipeline;

using namespace foundation::core;
using namespace pipeline;
using namespace foundation::vfs;
using namespace foundation::resource;
using namespace foundation::terrain;
namespace image = foundation::image;

namespace
{
    void RemoveTree()
    {
        FileDelete(u8"scratch_splatpipe_db/splat.rasset");
        FileDelete(u8"scratch_splatpipe_db/splat.pixels.bin");
        RemoveDirectory(u8"scratch_splatpipe_db");
    }
}

TEST_CASE("terrain.pipeline: SplatmapAsset cooks to a Splatmap that restores an identical raster")
{
    RegisterSplatmapAsset();
    RegisterSplatmapResourceTypes();
    RemoveTree();
    NativeFileSystem outMount(u8"scratch_splatpipe_db");

    // Author a painted raster the cook will carry.
    RefPtr<Splatmap> authored = MakeRef<Splatmap>(DefaultAllocator(), 16, 16);
    authored->SeedLayer0();
    (void)PaintWeight(*authored, 0.5f, 0.5f, 0.3f, 1u, 1.0f); // a blob of layer 1 in the centre

    Guid splatId;
    {
        foundation::content::ContentDatabase db(outMount, foundation::core::BinarySerializerFactory(),
                                              u8".rasset");
        // Cook in place into ONE instance (product guid == source guid): the source pixels sidecar
        // feeds the builder, which writes the cooked SplatmapSource object + pixels back. The
        // instance carries the cooked (product) type the factory reads - the TerrainSource pattern.
        auto* inst = db.RootGroup()->CreateInstance(u8"splat", SplatmapSource::StaticType());
        splatId = inst->Id();
        REQUIRE(inst->WriteData(kSplatStream, SplatmapSource::PixelBlob(*authored)).IsOk());

        SplatmapAsset sa;
        sa.width = 16;
        sa.height = 16;
        SplatmapAssetBuilder builder;
        NativeFileSystem srcMount(u8".");
        pipeline::AssetBuildContext ctx;
        ctx.sources = &srcMount;
        ctx.source = inst;
        ctx.output = inst;
        REQUIRE(builder.Build(sa, ctx).IsOk());
    }

    foundation::content::ContentDatabase db(outMount, foundation::core::BinarySerializerFactory(),
                                          u8".rasset");
    SplatmapFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    Proxy<Splatmap> loaded = manager.Bind<Splatmap>(splatId); // bind by the SOURCE guid
    REQUIRE(loaded);
    CHECK(loaded->Width() == 16);
    CHECK(loaded->Height() == 16);

    const Span<const u8> a = authored->Pixels();
    const Span<const u8> b = loaded->Pixels();
    REQUIRE(a.Size() == b.Size());
    bool identical = true;
    for (usize i = 0; i < a.Size(); ++i)
    {
        if (a[i] != b[i])
        {
            identical = false;
            break;
        }
    }
    CHECK(identical);
    CHECK(loaded->GetWeight(8, 8, 1) > 200); // the painted centre survived the cook

    RemoveTree();
}

TEST_CASE("terrain.pipeline: a SplatmapAsset with no pixels sidecar cooks a seeded base-layer raster")
{
    RegisterSplatmapAsset();
    RegisterSplatmapResourceTypes();
    RemoveTree();
    NativeFileSystem outMount(u8"scratch_splatpipe_db");

    Guid splatId;
    {
        foundation::content::ContentDatabase db(outMount, foundation::core::BinarySerializerFactory(),
                                              u8".rasset");
        auto* inst = db.RootGroup()->CreateInstance(u8"splat", SplatmapSource::StaticType());
        splatId = inst->Id(); // NO pixels sidecar written
        SplatmapAsset sa;
        sa.width = 8;
        sa.height = 8;

        SplatmapAssetBuilder builder;
        NativeFileSystem srcMount(u8".");
        pipeline::AssetBuildContext ctx;
        ctx.sources = &srcMount;
        ctx.source = inst;
        ctx.output = inst;
        REQUIRE(builder.Build(sa, ctx).IsOk());
    }

    foundation::content::ContentDatabase db(outMount, foundation::core::BinarySerializerFactory(),
                                          u8".rasset");
    SplatmapFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);
    Proxy<Splatmap> loaded = manager.Bind<Splatmap>(splatId);
    REQUIRE(loaded);
    CHECK(loaded->GetWeight(4, 4, 0) == 255); // seeded layer 0 at full weight
    CHECK(loaded->GetWeight(4, 4, 1) == 0);

    RemoveTree();
}

TEST_CASE("terrain.pipeline: SplatmapAsset imports an RGBA8 PNG to a Splatmap (decoder reuse)")
{
    // The user's point: PNG import reuses the image DECODER, not a TextureAsset/ImageAsset reference.
    // Author a known RGBA8 image -> PNG, cook a SplatmapAsset(fileName), and confirm the Splatmap
    // product is byte-identical (PNG is lossless for RGBA8) at the image's native size.
    RegisterSplatmapAsset();
    RegisterSplatmapResourceTypes();
    FileDelete(u8"scratch_splatimg/splat.png");
    RemoveDirectory(u8"scratch_splatimg");
    FileDelete(u8"scratch_splatimg_db/s.rasset");
    FileDelete(u8"scratch_splatimg_db/s.pixels.bin");
    RemoveDirectory(u8"scratch_splatimg_db");
    REQUIRE(CreateDirectory(u8"scratch_splatimg"));

    image::Image authored(4, 2, image::PixelFormat::RGBA8);
    Span<u8> ap = authored.PixelDataMut();
    for (usize i = 0; i < ap.Size(); ++i)
    {
        ap[i] = static_cast<u8>((i * 37 + 11) & 0xff); // deterministic RGBA pattern
    }
    REQUIRE(image::io::SaveImage(authored, u8"scratch_splatimg/splat.png",
                                 image::io::ImageFileFormat::PNG)
                .IsOk());

    NativeFileSystem outMount(u8"scratch_splatimg_db");
    Guid splatId;
    {
        foundation::content::ContentDatabase db(outMount, foundation::core::BinarySerializerFactory(),
                                              u8".rasset");
        auto* inst = db.RootGroup()->CreateInstance(u8"s", SplatmapSource::StaticType());
        splatId = inst->Id();
        SplatmapAsset sa;
        sa.fileName = foundation::vfs::SourcePath(u8"splat.png");
        SplatmapAssetBuilder builder;
        NativeFileSystem srcMount(u8"scratch_splatimg");
        pipeline::AssetBuildContext ctx;
        ctx.sources = &srcMount;
        ctx.output = inst;
        REQUIRE(builder.Build(sa, ctx).IsOk());
    }

    foundation::content::ContentDatabase db(outMount, foundation::core::BinarySerializerFactory(),
                                          u8".rasset");
    SplatmapFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);
    Proxy<Splatmap> loaded = manager.Bind<Splatmap>(splatId);
    REQUIRE(loaded);
    CHECK(loaded->Width() == 4);  // the PNG's NATIVE size (no resampling - splatmaps are arbitrary WxH)
    CHECK(loaded->Height() == 2);

    const Span<const u8> a = authored.PixelData();
    const Span<const u8> b = loaded->Pixels();
    REQUIRE(a.Size() == b.Size());
    bool identical = true;
    for (usize i = 0; i < a.Size(); ++i)
    {
        if (a[i] != b[i]) { identical = false; break; }
    }
    CHECK(identical);

    FileDelete(u8"scratch_splatimg/splat.png");
    RemoveDirectory(u8"scratch_splatimg");
    FileDelete(u8"scratch_splatimg_db/s.rasset");
    FileDelete(u8"scratch_splatimg_db/s.pixels.bin");
    RemoveDirectory(u8"scratch_splatimg_db");
}
