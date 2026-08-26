// TerrainEditorPage tests (headless): the pure stat-lines readout + the TerrainAsset v2 blob
// round-trip the page's undo snapshots ride (versioned payload - the page wraps its snapshots in
// BeginVersionedPayload so the top-K fields survive) + the v1 upgrade the page reads old assets
// through. The 3D preview + field grid need a live editor harness.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.heightfield;
import foundation.terrain.resource;
import pipeline.core;
import terrain.pipeline;
import editor.terrain;

using namespace foundation::core;
namespace hf = foundation::heightfield;
namespace terrain = foundation::terrain;

TEST_CASE("TerrainStatLines reports grid / chunks / palette / cast-shadows from a resolved product")
{
    RefPtr<hf::Heightfield> grid =
        MakeRef<hf::Heightfield>(DefaultAllocator(), 129, Float2{128.0f, 128.0f}, 0.0f, 20.0f);

    RefPtr<terrain::TerrainResource> res = MakeRef<terrain::TerrainResource>(DefaultAllocator());
    res->heightfield = grid.Get(); // direct in-memory product
    res->castShadows = true;
    res->palette.PushBack(terrain::TerrainResource::Layer{});
    res->palette.PushBack(terrain::TerrainResource::Layer{});

    const Array<String> lines = editor::TerrainStatLines(*res);
    REQUIRE(lines.Size() >= 4u);

    const auto has = [&](StringView needle)
    {
        for (const String& l : lines)
        {
            if (l.AsView() == needle)
            {
                return true;
            }
        }
        return false;
    };
    CHECK(has(u8"Grid: 129 x 129"));
    CHECK(has(u8"Chunks: 2 x 2 = 4")); // 129 -> 2x2 chunks
    CHECK(has(u8"Palette layers: 2"));
    CHECK(has(u8"Cast shadows: yes"));
}

TEST_CASE("TerrainStatLines is robust when the heightfield is unresolved")
{
    RefPtr<terrain::TerrainResource> res = MakeRef<terrain::TerrainResource>(DefaultAllocator());
    res->castShadows = false;
    const Array<String> lines = editor::TerrainStatLines(*res);
    bool unresolved = false;
    bool noShadows = false;
    for (const String& l : lines)
    {
        unresolved |= (l.AsView() == StringView(u8"Heightfield: unresolved"));
        noShadows |= (l.AsView() == StringView(u8"Cast shadows: no"));
    }
    CHECK(unresolved);
    CHECK(noShadows);
}

TEST_CASE("TerrainAsset v2 blob snapshot round-trips its authored fields (the undo path)")
{
    pipeline::RegisterTerrainAsset();
    pipeline::TerrainAsset a;
    a.heightfieldId = Guid{11, 22};
    a.weightsId = Guid{33, 44};
    a.baseAlbedoId = Guid{55, 66};
    a.baseTileScale = 24.0f;
    a.paletteAlbedoIds.PushBack(Guid{77, 88});
    a.paletteAlbedoIds.PushBack(Guid{});
    a.paletteTileScales.PushBack(16.0f);
    a.paletteTileScales.PushBack(48.0f);
    a.paletteTextureSize = 512;
    a.castShadows = false;

    // The page's snapshot shape: a bare BinarySerializer runs at ar.Version() == 0 and would take
    // the LEGACY read path, so snapshots are wrapped in the type's versioned payload.
    MemoryStream out;
    {
        BinarySerializer wr(out, SerializeMode::Write);
        BeginVersionedPayload(wr, pipeline::TerrainAsset::StaticType());
        a.Serialize(wr);
        REQUIRE(wr.IsOk());
    }

    MemoryStream in;
    (void)in.Write(out.Bytes().Data(), out.Bytes().Size());
    (void)in.Seek(0, SeekOrigin::Begin);
    pipeline::TerrainAsset b;
    {
        BinarySerializer rd(in, SerializeMode::Read);
        BeginVersionedPayload(rd, pipeline::TerrainAsset::StaticType());
        b.Serialize(rd);
        REQUIRE(rd.IsOk());
    }

    CHECK(b.heightfieldId == Guid{11, 22});
    CHECK(b.weightsId == Guid{33, 44});
    CHECK(b.baseAlbedoId == Guid{55, 66});
    CHECK(b.baseTileScale == doctest::Approx(24.0f));
    REQUIRE(b.paletteAlbedoIds.Size() == 2u);
    CHECK(b.paletteAlbedoIds[0] == Guid{77, 88});
    REQUIRE(b.paletteTileScales.Size() == 2u);
    CHECK(b.paletteTileScales[1] == doctest::Approx(48.0f));
    CHECK(b.paletteTextureSize == 512);
    CHECK(b.castShadows == false);
}

TEST_CASE("TerrainAsset reads a v1 (fixed-layer) payload: layer 0 -> base, layers 1.. -> palette")
{
    // A legacy envelope written WITHOUT a version scope (v1 payloads predate the gate).
    MemoryStream out;
    {
        BinarySerializer wr(out, SerializeMode::Write);
        pipeline::Asset envelope; // fileName header the asset serialize starts with
        envelope.Serialize(wr);
        Guid heightfieldId{1, 2};
        Guid splatmapId{3, 4};
        Array<Guid> layerAlbedoIds;
        layerAlbedoIds.PushBack(Guid{5, 6});
        layerAlbedoIds.PushBack(Guid{7, 8});
        Array<f32> layerTileScales;
        layerTileScales.PushBack(16.0f);
        layerTileScales.PushBack(4.0f);
        bool castShadows = false;
        Serialize(wr, "heightfieldId", heightfieldId);
        Serialize(wr, "splatmapId", splatmapId);
        Serialize(wr, "layerAlbedoIds", layerAlbedoIds);
        Serialize(wr, "layerTileScales", layerTileScales);
        Serialize(wr, "castShadows", castShadows);
        REQUIRE(wr.IsOk());
    }

    MemoryStream in;
    (void)in.Write(out.Bytes().Data(), out.Bytes().Size());
    (void)in.Seek(0, SeekOrigin::Begin);
    pipeline::TerrainAsset b;
    {
        BinarySerializer rd(in, SerializeMode::Read); // no version scope = legacy read
        b.Serialize(rd);
        REQUIRE(rd.IsOk());
    }
    CHECK(b.heightfieldId == Guid{1, 2});
    CHECK(b.weightsId == Guid{3, 4});    // splatmapId -> weightsId
    CHECK(b.baseAlbedoId == Guid{5, 6}); // layer 0 -> base
    CHECK(b.baseTileScale == doctest::Approx(16.0f));
    REQUIRE(b.paletteAlbedoIds.Size() == 1u); // layer 1 -> palette 0
    CHECK(b.paletteAlbedoIds[0] == Guid{7, 8});
    CHECK(b.paletteTileScales[0] == doctest::Approx(4.0f));
    CHECK(b.castShadows == false);
}
