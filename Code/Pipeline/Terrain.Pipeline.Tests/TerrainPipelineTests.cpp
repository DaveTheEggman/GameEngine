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
        asset.splatmapId = Guid{123, 456};             // D2: the splat weight map (id pass-through)
        asset.layerAlbedoIds.PushBack(Guid{321, 654}); // non-nil so the albedo ref binds
        asset.layerAlbedoIds.PushBack(Guid{});
        asset.layerTileScales.PushBack(4.0f);
        asset.layerTileScales.PushBack(8.0f);
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
    CHECK(terrain->LayerCount() == 2u);
    CHECK(terrain->layers[0].tileScale == doctest::Approx(4.0f));
    CHECK(terrain->layers[1].tileScale == doctest::Approx(8.0f));
    REQUIRE(terrain->heightfield);
    CHECK(terrain->heightfield->Size() == 65);
    // D2 splat ids round-trip through cook -> source -> factory -> a BOUND ref (a non-nil id binds a
    // proxy even with no texture factory; GPU resolution + blending is the backend probe's job).
    CHECK(terrain->splatmap.IsBound());       // splatmapId carried
    CHECK(terrain->layers[0].albedo.IsBound()); // non-nil layer albedo carried
    CHECK_FALSE(terrain->layers[1].albedo.IsBound()); // nil id stays unbound

    RemoveTree();
}
