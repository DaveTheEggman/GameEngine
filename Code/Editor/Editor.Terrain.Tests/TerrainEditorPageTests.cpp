// TerrainEditorPage tests (headless): the pure stat-lines readout + the TerrainAsset blob round-trip
// the page's undo snapshots ride. The 3D preview + field grid need a live editor harness.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.heightfield;
import foundation.terrain.resource;
import terrain.pipeline;
import editor.terrain;

using namespace foundation::core;
namespace hf = foundation::heightfield;
namespace terrain = foundation::terrain;

TEST_CASE("TerrainStatLines reports grid / chunks / layers / cast-shadows from a resolved product")
{
    RefPtr<hf::Heightfield> grid =
        MakeRef<hf::Heightfield>(DefaultAllocator(), 129, Float2{128.0f, 128.0f}, 0.0f, 20.0f);

    RefPtr<terrain::TerrainResource> res = MakeRef<terrain::TerrainResource>(DefaultAllocator());
    res->heightfield = grid.Get(); // direct in-memory product
    res->castShadows = true;
    res->layers.PushBack(terrain::TerrainResource::Layer{});
    res->layers.PushBack(terrain::TerrainResource::Layer{});

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
    CHECK(has(u8"Layers: 2"));
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

TEST_CASE("TerrainAsset blob snapshot round-trips its authored fields (the undo path)")
{
    pipeline::TerrainAsset a;
    a.heightfieldId = Guid{11, 22};
    a.splatmapId = Guid{33, 44};
    a.layerAlbedoIds.PushBack(Guid{55, 66});
    a.layerAlbedoIds.PushBack(Guid{});
    a.layerTileScales.PushBack(16.0f);
    a.layerTileScales.PushBack(48.0f);
    a.castShadows = false;

    MemoryStream out;
    BinarySerializer wr(out, SerializeMode::Write);
    a.Serialize(wr);

    MemoryStream in;
    (void)in.Write(out.Bytes().Data(), out.Bytes().Size());
    (void)in.Seek(0, SeekOrigin::Begin);
    BinarySerializer rd(in, SerializeMode::Read);
    pipeline::TerrainAsset b;
    b.Serialize(rd);

    CHECK(b.heightfieldId == Guid{11, 22});
    CHECK(b.splatmapId == Guid{33, 44});
    REQUIRE(b.layerAlbedoIds.Size() == 2u);
    CHECK(b.layerAlbedoIds[0] == Guid{55, 66});
    REQUIRE(b.layerTileScales.Size() == 2u);
    CHECK(b.layerTileScales[1] == doctest::Approx(48.0f));
    CHECK(b.castShadows == false);
}
