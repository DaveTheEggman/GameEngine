// The cooked terrain resource: a TerrainSource serialize round-trip, and a full content-DB build
// through the manager that RESOLVES the referenced heightfield (proving the shared Ref) while the
// texture refs stay unbound (no GPU factory headless).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.heightfield;
import foundation.heightfield.resource;
import foundation.terrain.resource;

using namespace foundation::core;
using namespace foundation::vfs;
using namespace foundation::resource;
using namespace foundation::terrain;
namespace hf = foundation::heightfield;

namespace
{
    void RemoveTree()
    {
        FileDelete(u8"scratch_terrain_res_db/hf.rasset");
        FileDelete(u8"scratch_terrain_res_db/hf.heights.bin");
        FileDelete(u8"scratch_terrain_res_db/terrain.rasset");
        RemoveDirectory(u8"scratch_terrain_res_db");
    }
}

TEST_CASE("terrain resource: TerrainSource serialize round-trips its references + params")
{
    TerrainSource a;
    a.heightfieldId = Guid(1, 2);
    a.splatmapId = Guid(3, 4);
    a.layerAlbedoIds.PushBack(Guid(5, 6));
    a.layerAlbedoIds.PushBack(Guid(7, 8));
    a.layerTileScales.PushBack(4.0f);
    a.layerTileScales.PushBack(8.0f);
    a.castShadows = false;

    MemoryStream stream;
    {
        BinarySerializer ar(stream, SerializeMode::Write);
        a.Serialize(ar);
        REQUIRE(ar.IsOk());
    }
    (void)stream.Seek(0, SeekOrigin::Begin);
    TerrainSource b;
    {
        BinarySerializer ar(stream, SerializeMode::Read);
        b.Serialize(ar);
        REQUIRE(ar.IsOk());
    }
    CHECK(b.heightfieldId == Guid(1, 2));
    CHECK(b.splatmapId == Guid(3, 4));
    CHECK(b.layerAlbedoIds.Size() == 2u);
    CHECK(b.layerAlbedoIds[1] == Guid(7, 8));
    CHECK(b.layerTileScales.Size() == 2u);
    CHECK(b.layerTileScales[0] == doctest::Approx(4.0f));
    CHECK(b.castShadows == false);
}

TEST_CASE("terrain resource: builds through the manager and resolves the shared heightfield")
{
    hf::RegisterHeightfieldResourceTypes();
    RegisterTerrainResourceTypes();
    RemoveTree();
    NativeFileSystem mount(u8"scratch_terrain_res_db");

    Guid heightfieldId;
    Guid terrainId;
    {
        foundation::content::ContentDatabase db(mount, foundation::core::BinarySerializerFactory(),
                                              u8".rasset");
        // A cooked heightfield.
        auto* hfInst = db.RootGroup()->CreateInstance(u8"hf", hf::HeightfieldSource::StaticType());
        heightfieldId = hfInst->Id();
        RefPtr<hf::Heightfield> grid =
            MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
        hf::HeightfieldSource hfSrc;
        hf::HeightfieldSource::FromHeightfield(*grid, hfSrc);
        REQUIRE(hfInst->WriteObject(hfSrc).IsOk());
        REQUIRE(
            hfInst->WriteData(hf::kHeightStream, hf::HeightfieldSource::HeightBlob(*grid)).IsOk());

        // A terrain referencing that heightfield + two (texture-less) layers.
        auto* tInst = db.RootGroup()->CreateInstance(u8"terrain", TerrainSource::StaticType());
        terrainId = tInst->Id();
        TerrainSource tSrc;
        tSrc.heightfieldId = heightfieldId;
        tSrc.layerAlbedoIds.PushBack(Guid{}); // nil albedo (no GPU factory here)
        tSrc.layerAlbedoIds.PushBack(Guid{});
        tSrc.layerTileScales.PushBack(4.0f);
        tSrc.layerTileScales.PushBack(8.0f);
        tSrc.castShadows = false;
        REQUIRE(tInst->WriteObject(tSrc).IsOk());
    }

    foundation::content::ContentDatabase db(mount, foundation::core::BinarySerializerFactory(),
                                          u8".rasset");
    hf::HeightfieldFactory heightfieldFactory;
    TerrainFactory terrainFactory;
    ResourceManager manager(db);
    manager.AddFactory(&heightfieldFactory);
    manager.AddFactory(&terrainFactory);

    Proxy<Terrain> terrain = manager.Bind<Terrain>(terrainId);
    REQUIRE(terrain);
    CHECK(terrain->castShadows == false);
    CHECK(terrain->LayerCount() == 2u);
    CHECK(terrain->layers[0].tileScale == doctest::Approx(4.0f));
    CHECK(terrain->layers[1].tileScale == doctest::Approx(8.0f));
    // The shared heightfield resolved through the same manager.
    REQUIRE(terrain->heightfield);
    CHECK(terrain->heightfield->Size() == 65);
    CHECK(terrain->heightfield->GetHeightAt(0.0f, 0.0f) == doctest::Approx(0.0f));

    RemoveTree();
}
