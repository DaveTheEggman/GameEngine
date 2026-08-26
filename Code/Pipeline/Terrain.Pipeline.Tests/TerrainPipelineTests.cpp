// Full terrain asset pipeline: author a TerrainAsset (referencing a cooked heightfield + layers) ->
// cook with TerrainAssetBuilder into an output content DB -> load the Terrain through the manager and
// confirm it resolves the SHARED heightfield + carries the layers/flags (a reference pass-through).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import pipeline.core;
import foundation.heightfield;
import foundation.heightfield.resource;
import foundation.terrain.resource;
import terrain.pipeline;

using namespace foundation::core;
using namespace pipeline;
using namespace foundation::vfs;
using namespace foundation::resource;
using namespace foundation::terrain;
namespace hf = foundation::heightfield;

namespace
{
    void RemoveTree()
    {
        FileDelete(u8"scratch_terrainpipe_db/hf.rasset");
        FileDelete(u8"scratch_terrainpipe_db/hf.heights.bin");
        FileDelete(u8"scratch_terrainpipe_db/terrain.rasset");
        FileDelete(u8"scratch_terrainpipe_db/terrain.palette.bin");
        FileDelete(u8"scratch_terrainpipe_db/terrain.palette.normal.bin");
        FileDelete(u8"scratch_terrainpipe_db/terrain.palette.orm.bin");
        RemoveDirectory(u8"scratch_terrainpipe_db");
    }
}

TEST_CASE("terrain.pipeline: TerrainAsset cooks to a Terrain that resolves the shared heightfield")
{
    hf::RegisterHeightfieldResourceTypes();
    RegisterTerrainAsset();
    RegisterTerrainResourceTypes();
    RemoveTree();
    NativeFileSystem outMount(u8"scratch_terrainpipe_db");

    Guid heightfieldId;
    Guid terrainId;
    {
        foundation::content::ContentDatabase db(outMount, foundation::core::BinarySerializerFactory(),
                                              u8".rasset");
        // A cooked heightfield the terrain will reference.
        auto* hfInst = db.RootGroup()->CreateInstance(u8"hf", hf::HeightfieldSource::StaticType());
        heightfieldId = hfInst->Id();
        RefPtr<hf::Heightfield> grid =
            MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
        hf::HeightfieldSource hfSrc;
        hf::HeightfieldSource::FromHeightfield(*grid, hfSrc);
        REQUIRE(hfInst->WriteObject(hfSrc).IsOk());
        REQUIRE(
            hfInst->WriteData(hf::kHeightStream, hf::HeightfieldSource::HeightBlob(*grid)).IsOk());

        // Cook the terrain asset through the builder.
        auto* tInst = db.RootGroup()->CreateInstance(u8"terrain", TerrainSource::StaticType());
        terrainId = tInst->Id();
        TerrainAsset asset;
        asset.heightfieldId = heightfieldId;
        asset.weightsId = Guid{123, 456};                // the top-K weights (id pass-through)
        asset.baseAlbedoId = Guid{111, 222};             // the BASE layer albedo
        asset.baseTileScale = 16.0f;
        asset.paletteAlbedoIds.PushBack(Guid{321, 654}); // non-nil so the albedo ref binds
        asset.paletteAlbedoIds.PushBack(Guid{});
        asset.paletteTileScales.PushBack(4.0f);
        asset.paletteTileScales.PushBack(8.0f);
        asset.castShadows = false;

        TerrainAssetBuilder builder;
        NativeFileSystem srcMount(u8".");
        pipeline::AssetBuildContext ctx;
        ctx.sources = &srcMount;
        ctx.output = tInst;
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    foundation::content::ContentDatabase db(outMount, foundation::core::BinarySerializerFactory(),
                                          u8".rasset");
    hf::HeightfieldFactory heightfieldFactory;
    TerrainFactory terrainFactory;
    ResourceManager manager(db);
    manager.AddFactory(&heightfieldFactory);
    manager.AddFactory(&terrainFactory);

    Proxy<TerrainResource> terrain = manager.Bind<TerrainResource>(terrainId);
    REQUIRE(terrain);
    CHECK(terrain->castShadows == false);
    CHECK(terrain->PaletteCount() == 2u);
    CHECK(terrain->base.tileScale == doctest::Approx(16.0f));
    CHECK(terrain->palette[0].tileScale == doctest::Approx(4.0f));
    CHECK(terrain->palette[1].tileScale == doctest::Approx(8.0f));
    REQUIRE(terrain->heightfield);
    CHECK(terrain->heightfield->Size() == 65);
    // Splat ids round-trip through cook -> source -> factory -> a BOUND ref (a non-nil id binds a
    // proxy even with no texture factory; GPU resolution + blending is the backend probe's job).
    CHECK(terrain->weights.IsBound());            // weightsId carried
    CHECK(terrain->base.albedo.IsBound());        // base albedo carried
    CHECK(terrain->palette[0].albedo.IsBound());  // non-nil palette albedo carried
    CHECK_FALSE(terrain->palette[1].albedo.IsBound()); // nil id stays unbound
    // The factory stamps each ref's source id (product guid == source guid) - the editor resolves
    // the heightfield asset from this ref chain (no reverse lookup).
    CHECK(terrain->heightfield.id == heightfieldId);
    CHECK(terrain->weights.id == Guid{123, 456});
    CHECK(terrain->base.albedo.id == Guid{111, 222});
    CHECK(terrain->palette[0].albedo.id == Guid{321, 654});

    RemoveTree();
}

TEST_CASE("terrain.pipeline: per-layer normal + ORM ids round-trip; arrays built on demand (P0)")
{
    hf::RegisterHeightfieldResourceTypes();
    RegisterTerrainAsset();
    RegisterTerrainResourceTypes();
    RemoveTree();
    NativeFileSystem outMount(u8"scratch_terrainpipe_db");

    Guid terrainId;
    u32 sliceSize = 0, mipCount = 0;
    Array<u8> cookedNormal; // the raw cooked normal stream (header + texels)
    bool ormStreamPresent = true;
    {
        foundation::content::ContentDatabase db(outMount, foundation::core::BinarySerializerFactory(),
                                              u8".rasset");
        auto* hfInst = db.RootGroup()->CreateInstance(u8"hf", hf::HeightfieldSource::StaticType());
        RefPtr<hf::Heightfield> grid =
            MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
        hf::HeightfieldSource hfSrc;
        hf::HeightfieldSource::FromHeightfield(*grid, hfSrc);
        REQUIRE(hfInst->WriteObject(hfSrc).IsOk());
        REQUIRE(
            hfInst->WriteData(hf::kHeightStream, hf::HeightfieldSource::HeightBlob(*grid)).IsOk());

        auto* tInst = db.RootGroup()->CreateInstance(u8"terrain", TerrainSource::StaticType());
        terrainId = tInst->Id();
        TerrainAsset asset;
        asset.heightfieldId = hfInst->Id();
        asset.baseAlbedoId = Guid{111, 222};
        asset.baseNormalId = Guid{11, 22}; // base normal + ORM ids (unresolvable -> flat/default)
        asset.baseOrmId = Guid{33, 44};
        asset.baseTileScale = 16.0f;
        asset.paletteTextureSize = 64; // keep the cook small/fast
        asset.paletteAlbedoIds.PushBack(Guid{321, 654});
        asset.paletteAlbedoIds.PushBack(Guid{});
        asset.paletteNormalIds.PushBack(Guid{55, 66}); // layer 0 HAS a normal -> array built
        asset.paletteNormalIds.PushBack(Guid{});       // layer 1 nil -> default flat slice
        // NO ORM ids on any palette layer -> the ORM array must NOT be written.
        asset.paletteTileScales.PushBack(4.0f);
        asset.paletteTileScales.PushBack(8.0f);

        TerrainAssetBuilder builder;
        NativeFileSystem srcMount(u8".");
        pipeline::AssetBuildContext ctx;
        ctx.sources = &srcMount;
        ctx.output = tInst;
        REQUIRE(builder.Build(asset, ctx).IsOk());

        // The normal stream exists (a layer used it); the ORM stream is absent (R4).
        UniquePtr<IStream> nrm = tInst->ReadData(kPaletteNormalStream);
        REQUIRE(nrm);
        cookedNormal.Resize(static_cast<usize>(nrm->Size()));
        REQUIRE(nrm->Read(cookedNormal.Data(), static_cast<u64>(cookedNormal.Size())) ==
                cookedNormal.Size());
        ormStreamPresent = static_cast<bool>(tInst->ReadData(kPaletteOrmStream));
    }

    CHECK_FALSE(ormStreamPresent); // no ORM on any layer -> no array (on demand, R4)

    // The nil palette layer's normal slice defaulted to flat (128,128,255,255) - R4 default fill.
    REQUIRE(cookedNormal.Size() > sizeof(u32) * 3);
    MemCopy(&sliceSize, cookedNormal.Data(), sizeof(u32));
    MemCopy(&mipCount, cookedNormal.Data() + sizeof(u32), sizeof(u32));
    const usize sliceBytes = TerrainPaletteData::SliceBytes(sliceSize, mipCount);
    const usize slice1 = sizeof(u32) * 3 + sliceBytes * 1; // layer 1 (nil), mip 0, first texel
    CHECK(cookedNormal[slice1 + 0] == 128);
    CHECK(cookedNormal[slice1 + 1] == 128);
    CHECK(cookedNormal[slice1 + 2] == 255);
    CHECK(cookedNormal[slice1 + 3] == 255);

    // Load: refs bound + ids round-trip; paletteData has the normal array but NOT the ORM array.
    foundation::content::ContentDatabase db(outMount, foundation::core::BinarySerializerFactory(),
                                          u8".rasset");
    hf::HeightfieldFactory heightfieldFactory;
    TerrainFactory terrainFactory;
    ResourceManager manager(db);
    manager.AddFactory(&heightfieldFactory);
    manager.AddFactory(&terrainFactory);

    Proxy<TerrainResource> terrain = manager.Bind<TerrainResource>(terrainId);
    REQUIRE(terrain);
    CHECK(terrain->base.normal.IsBound());
    CHECK(terrain->base.normal.id == Guid{11, 22});
    CHECK(terrain->base.orm.IsBound());
    CHECK(terrain->base.orm.id == Guid{33, 44});
    CHECK(terrain->palette[0].normal.IsBound());
    CHECK(terrain->palette[0].normal.id == Guid{55, 66});
    CHECK_FALSE(terrain->palette[1].normal.IsBound()); // nil id stays unbound
    CHECK_FALSE(terrain->palette[0].orm.IsBound());    // no ORM ids authored
    REQUIRE(terrain->paletteData);
    CHECK(terrain->paletteData->HasNormal());   // normal array present (a layer used it)
    CHECK_FALSE(terrain->paletteData->HasOrm()); // ORM array absent

    RemoveTree();
}

TEST_CASE("terrain.pipeline: no normal/ORM maps -> no arrays (compat, P0)")
{
    hf::RegisterHeightfieldResourceTypes();
    RegisterTerrainAsset();
    RegisterTerrainResourceTypes();
    RemoveTree();
    NativeFileSystem outMount(u8"scratch_terrainpipe_db");

    Guid terrainId;
    {
        foundation::content::ContentDatabase db(outMount, foundation::core::BinarySerializerFactory(),
                                              u8".rasset");
        auto* hfInst = db.RootGroup()->CreateInstance(u8"hf", hf::HeightfieldSource::StaticType());
        RefPtr<hf::Heightfield> grid =
            MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
        hf::HeightfieldSource hfSrc;
        hf::HeightfieldSource::FromHeightfield(*grid, hfSrc);
        REQUIRE(hfInst->WriteObject(hfSrc).IsOk());
        REQUIRE(
            hfInst->WriteData(hf::kHeightStream, hf::HeightfieldSource::HeightBlob(*grid)).IsOk());

        auto* tInst = db.RootGroup()->CreateInstance(u8"terrain", TerrainSource::StaticType());
        terrainId = tInst->Id();
        TerrainAsset asset;
        asset.heightfieldId = hfInst->Id();
        asset.baseAlbedoId = Guid{111, 222};
        asset.paletteTextureSize = 64;
        asset.paletteAlbedoIds.PushBack(Guid{321, 654}); // albedo only, no normal/ORM anywhere
        asset.paletteTileScales.PushBack(4.0f);

        TerrainAssetBuilder builder;
        NativeFileSystem srcMount(u8".");
        pipeline::AssetBuildContext ctx;
        ctx.sources = &srcMount;
        ctx.output = tInst;
        REQUIRE(builder.Build(asset, ctx).IsOk());
        CHECK_FALSE(static_cast<bool>(tInst->ReadData(kPaletteNormalStream)));
        CHECK_FALSE(static_cast<bool>(tInst->ReadData(kPaletteOrmStream)));
    }

    foundation::content::ContentDatabase db(outMount, foundation::core::BinarySerializerFactory(),
                                          u8".rasset");
    hf::HeightfieldFactory heightfieldFactory;
    TerrainFactory terrainFactory;
    ResourceManager manager(db);
    manager.AddFactory(&heightfieldFactory);
    manager.AddFactory(&terrainFactory);

    Proxy<TerrainResource> terrain = manager.Bind<TerrainResource>(terrainId);
    REQUIRE(terrain);
    REQUIRE(terrain->paletteData);
    CHECK(terrain->paletteData->IsValid());     // albedo array present
    CHECK_FALSE(terrain->paletteData->HasNormal());
    CHECK_FALSE(terrain->paletteData->HasOrm());

    RemoveTree();
}
