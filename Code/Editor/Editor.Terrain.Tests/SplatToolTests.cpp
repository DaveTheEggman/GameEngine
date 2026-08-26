// TerrainSplatTool tests (headless, top-K model): a whole paint gesture scripted through
// ViewportToolInput. Drives press -> drag -> release over an in-memory terrain (heightfield +
// SplatWeights) and asserts the selected PALETTE layer rises out of the base, ONE both-raster
// command undoes/redoes it, the eraser reveals the base, the brush is unavailable with no weights +
// refuses edits under Simulate, and the persist closure converts the source to embedded (BOTH
// sidecars) so the paint survives a re-cook.

#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.content;
import foundation.vfs;      // NativeFileSystem (the persist round-trip test)
import foundation.resource;
import foundation.scene;
import foundation.heightfield;
import foundation.terrain.resource;
import engine.terrain;
import pipeline.core;
import terrain.pipeline;    // SplatmapAsset + builder (the SOURCE envelope the persist rewrites)
import editor.core;
import editor.viewporttools;
import editor.terrain;

using namespace foundation::core;
namespace scene = foundation::scene;
namespace hf = foundation::heightfield;
namespace terrain = foundation::terrain;

namespace
{
    class FakeAssetEditSink final : public editor::IAssetEditSink
    {
    public:
        void RegisterAssetEdit(const Guid& assetId,
                               Function<Status(foundation::content::ContentDatabase&)> persist) override
        {
            ++count;
            lastId = assetId;
            hasPersist = static_cast<bool>(persist);
            lastPersist = Move(persist); // captured so tests can run it against a real DB
        }
        i32 count = 0;
        Guid lastId;
        bool hasPersist = false;
        Function<Status(foundation::content::ContentDatabase&)> lastPersist;
    };

    editor::ViewportToolInput CenterRay(f32 deltaSeconds)
    {
        editor::ViewportToolInput in;
        in.ray.origin = Float3{0.0f, 100.0f, 0.0f};
        in.ray.direction = Float3{0.0f, -1.0f, 0.0f};
        in.pointerValid = true;
        in.pointerOver = true;
        in.deltaSeconds = deltaSeconds;
        return in;
    }

    struct Fixture
    {
        scene::Scene scene;
        RefPtr<hf::Heightfield> grid;
        RefPtr<terrain::SplatWeights> weights;
        RefPtr<terrain::TerrainResource> res;

        Fixture()
        {
            engine::terrain::AddTerrainSceneManagers(scene);
            auto* mgr = scene.GetSystem<engine::terrain::TerrainComponentManager>();
            grid = MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
            weights = MakeRef<terrain::SplatWeights>(DefaultAllocator(), 32, 32);
            res = MakeRef<terrain::TerrainResource>(DefaultAllocator());
            res->heightfield = grid.Get();
            res->weights = weights.Get();
            res->weights.SetId(Guid{5, 55}); // the source asset guid the tool persists back to
            const scene::EntityHandle e = scene.CreateEntity(u8"terrain");
            engine::terrain::TerrainComponent& c = mgr->Add(e);
            c.terrain = res.Get();
            scene.Start();
        }
    };
}

TEST_CASE("terrain splat: a press-drag-release stroke raises the selected palette layer")
{
    Fixture fx;
    editor::EditorCommandStack commands;
    FakeAssetEditSink sink;
    editor::TerrainSplatTool tool(fx.scene, commands, &sink);
    tool.SetPaletteIndex(1); // paint palette layer 1 over the (implicit) base

    CHECK(tool.IsAvailable());
    CHECK(fx.weights->BaseWeight(16, 16) == 255); // pure base before painting
    CHECK(fx.weights->WeightOfLayer(16, 16, 1) == 0);
    const u64 v0 = fx.weights->Version();

    editor::ViewportToolInput press = CenterRay(0.5f);
    press.leftPressed = true;
    press.leftDown = true;
    CHECK(tool.Update(press)); // consumed: the brush owns the click

    const u8 dab1 = fx.weights->WeightOfLayer(16, 16, 1);
    CHECK(dab1 > 0);                              // palette 1 rose at the centre
    CHECK(fx.weights->BaseWeight(16, 16) < 255);  // the base receded by the gained amount
    CHECK(fx.weights->Version() > v0);            // paint bumped the version (GPU re-upload)

    editor::ViewportToolInput drag = CenterRay(0.5f);
    drag.leftDown = true;
    CHECK(tool.Update(drag));
    CHECK(fx.weights->WeightOfLayer(16, 16, 1) > dab1); // more after another dab

    editor::ViewportToolInput release = CenterRay(0.5f);
    release.leftReleased = true;
    (void)tool.Update(release);

    CHECK(commands.CanUndo());
    CHECK(sink.count == 1);
    CHECK(sink.lastId == Guid{5, 55});
    CHECK(sink.hasPersist);
}

TEST_CASE("terrain splat: one command per stroke undoes/redoes BOTH rasters")
{
    Fixture fx;
    editor::EditorCommandStack commands;
    FakeAssetEditSink sink;
    editor::TerrainSplatTool tool(fx.scene, commands, &sink);
    tool.SetPaletteIndex(2);

    editor::ViewportToolInput press = CenterRay(1.0f);
    press.leftPressed = true;
    press.leftDown = true;
    (void)tool.Update(press);
    editor::ViewportToolInput release = CenterRay(1.0f);
    release.leftReleased = true;
    (void)tool.Update(release);

    const u8 painted = fx.weights->WeightOfLayer(16, 16, 2);
    REQUIRE(painted > 0);
    // The paint claimed a slot whose INDEX byte is now 2 - undo must restore that raster too.
    bool hasIndex2 = false;
    for (u32 k = 0; k < terrain::kSplatSlotCount; ++k)
    {
        hasIndex2 = hasIndex2 || (fx.weights->SlotIndex(16, 16, k) == 2);
    }
    REQUIRE(hasIndex2);

    commands.Undo();
    CHECK(fx.weights->WeightOfLayer(16, 16, 2) == 0); // weights restored
    CHECK(fx.weights->BaseWeight(16, 16) == 255);     // pure base again
    for (u32 k = 0; k < terrain::kSplatSlotCount; ++k)
    {
        CHECK(fx.weights->SlotIndex(16, 16, k) == 0); // the index raster restored too
    }

    commands.Redo();
    CHECK(fx.weights->WeightOfLayer(16, 16, 2) == painted); // redo replays the after-state
}

TEST_CASE("terrain splat: the eraser fades paint back to the base")
{
    Fixture fx;
    editor::EditorCommandStack commands;
    editor::TerrainSplatTool tool(fx.scene, commands, nullptr);
    tool.SetPaletteIndex(3);
    tool.SetStrength(1.0f);

    // Lay down a real amount of paint (press + several drag dabs).
    editor::ViewportToolInput press = CenterRay(1.0f);
    press.leftPressed = true;
    press.leftDown = true;
    (void)tool.Update(press);
    for (i32 i = 0; i < 8; ++i)
    {
        editor::ViewportToolInput drag = CenterRay(1.0f);
        drag.leftDown = true;
        (void)tool.Update(drag);
    }
    editor::ViewportToolInput release = CenterRay(1.0f);
    release.leftReleased = true;
    (void)tool.Update(release);
    const u8 painted = fx.weights->WeightOfLayer(16, 16, 3);
    REQUIRE(painted > 20);

    // Erase over the same spot: layer 3 fades, the base comes back.
    tool.SetEraser(true);
    CHECK(tool.IsEraser());
    editor::ViewportToolInput epress = CenterRay(1.0f);
    epress.leftPressed = true;
    epress.leftDown = true;
    (void)tool.Update(epress);
    for (i32 i = 0; i < 8; ++i)
    {
        editor::ViewportToolInput drag = CenterRay(1.0f);
        drag.leftDown = true;
        (void)tool.Update(drag);
    }
    editor::ViewportToolInput erelease = CenterRay(1.0f);
    erelease.leftReleased = true;
    (void)tool.Update(erelease);

    CHECK(fx.weights->WeightOfLayer(16, 16, 3) < painted);
    CHECK(fx.weights->BaseWeight(16, 16) > 0);
}

TEST_CASE("terrain splat: unavailable with no weights, and refuses edits while editingLocked")
{
    // A terrain with a heightfield but NO weights raster: the paint tool is not relevant.
    scene::Scene bare;
    engine::terrain::AddTerrainSceneManagers(bare);
    auto* mgr = bare.GetSystem<engine::terrain::TerrainComponentManager>();
    RefPtr<hf::Heightfield> grid =
        MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
    auto res = MakeRef<terrain::TerrainResource>(DefaultAllocator());
    res->heightfield = grid.Get(); // no weights
    const scene::EntityHandle e = bare.CreateEntity(u8"terrain");
    mgr->Add(e).terrain = res.Get();
    bare.Start();
    editor::EditorCommandStack commandsA;
    editor::TerrainSplatTool bareTool(bare, commandsA, nullptr);
    CHECK_FALSE(bareTool.IsAvailable());

    // With weights but editingLocked (Simulate): a press does nothing.
    Fixture fx;
    editor::EditorCommandStack commands;
    editor::TerrainSplatTool tool(fx.scene, commands, nullptr);
    tool.SetPaletteIndex(1);
    editor::ViewportToolInput locked = CenterRay(0.5f);
    locked.leftPressed = true;
    locked.leftDown = true;
    locked.editingLocked = true;
    (void)tool.Update(locked);
    CHECK(fx.weights->WeightOfLayer(16, 16, 1) == 0); // no edit under lock
    CHECK_FALSE(commands.CanUndo());
}

// Editable-source convention: painting an IMPORTED (PNG-backed) splatmap must persist durably -
// the closure rewrites the SOURCE envelope (dims synced to the raster, fileName cleared =
// converted to embedded) plus BOTH sidecars ("pixels" weights + "indices"), so a re-cook keeps the
// paint instead of regenerating from the file (re-import explicitly resets).
TEST_CASE("terrain splat: a save converts an imported splatmap to embedded and survives a re-cook")
{
    using foundation::vfs::NativeFileSystem;
    namespace content = foundation::content;

    const StringView dbDir = u8"scratch_splat_persist_db";
    FileDelete(u8"scratch_splat_persist_db/splat.rasset");
    FileDelete(u8"scratch_splat_persist_db/splat.pixels.bin");
    FileDelete(u8"scratch_splat_persist_db/splat.indices.bin");
    RemoveDirectory(dbDir);
    NativeFileSystem mount(dbDir);

    // The SOURCE asset: imported-style (fileName set) with STALE dims - the raster is 32x32.
    Guid id;
    {
        content::ContentDatabase db(mount, foundation::core::BinarySerializerFactory(),
                                    u8".rasset");
        pipeline::RegisterSplatmapAsset();
        terrain::RegisterSplatmapResourceTypes();
        auto* inst =
            db.RootGroup()->CreateInstance(u8"splat", pipeline::SplatmapAsset::StaticType());
        REQUIRE(inst != nullptr);
        id = inst->Id();
        pipeline::SplatmapAsset src;
        src.width = 8; // stale: the import decoded at native size, the envelope never knew
        src.height = 8;
        src.fileName = foundation::vfs::SourcePath(u8"weights.png"); // imported-style
        REQUIRE(inst->WriteObject(src).IsOk());
    }

    // Paint a stroke on the shared runtime raster bound to that source id.
    Fixture fx;
    fx.res->weights.SetId(id);
    FakeAssetEditSink sink;
    editor::EditorCommandStack commands;
    editor::TerrainSplatTool tool(fx.scene, commands, &sink);
    tool.SetPaletteIndex(1);

    editor::ViewportToolInput press = CenterRay(0.5f);
    press.leftPressed = true;
    press.leftDown = true;
    (void)tool.Update(press);
    editor::ViewportToolInput release = CenterRay(0.0f);
    release.leftReleased = true;
    (void)tool.Update(release);
    REQUIRE(sink.lastPersist);
    CHECK(sink.lastId == id);
    const u8 painted = fx.weights->WeightOfLayer(16, 16, 1);
    REQUIRE(painted > 0); // the stroke raised palette 1

    // Drain: envelope converts to embedded with the raster's true dims, then re-cook into a
    // SEPARATE cooked DB under the SAME guid (product guid == source guid - the production shape;
    // the cooked instance carries the product-source type the factory reads).
    const StringView cookedDir = u8"scratch_splat_persist_cooked";
    FileDelete(u8"scratch_splat_persist_cooked/splat.rasset");
    FileDelete(u8"scratch_splat_persist_cooked/splat.pixels.bin");
    FileDelete(u8"scratch_splat_persist_cooked/splat.indices.bin");
    RemoveDirectory(cookedDir);
    NativeFileSystem cookedMount(cookedDir);
    {
        content::ContentDatabase db(mount, foundation::core::BinarySerializerFactory(),
                                    u8".rasset");
        REQUIRE(sink.lastPersist(db).IsOk());
        content::Instance* inst = db.GetInstance(id);
        REQUIRE(inst != nullptr);
        RefPtr<ISerializable> object = inst->ReadObject();
        auto* asset = Cast<pipeline::SplatmapAsset>(object.Get());
        REQUIRE(asset != nullptr);               // envelope is STILL a SplatmapAsset
        CHECK(asset->fileName.View().IsEmpty()); // converted to embedded (sidecars = truth)
        CHECK(asset->width == 32);               // dims synced to the painted raster
        CHECK(asset->height == 32);

        content::ContentDatabase cookedDb(cookedMount,
                                          foundation::core::BinarySerializerFactory(),
                                          u8".rasset");
        content::Instance* cookedInst = cookedDb.RootGroup()->CreateInstanceWithId(
            id, u8"splat", terrain::SplatWeightsSource::StaticType());
        REQUIRE(cookedInst != nullptr);

        pipeline::SplatmapAssetBuilder builder;
        NativeFileSystem srcMount(u8".");
        pipeline::AssetBuildContext ctx;
        ctx.sources = &srcMount;
        ctx.source = inst;
        ctx.output = cookedInst;
        REQUIRE(builder.Build(*asset, ctx).IsOk());
    }

    // Bind the cooked product through a FRESH db: the paint (weights AND indices) survived.
    {
        content::ContentDatabase cookedDb(cookedMount,
                                          foundation::core::BinarySerializerFactory(),
                                          u8".rasset");
        terrain::SplatWeightsFactory factory;
        foundation::resource::ResourceManager manager(cookedDb);
        manager.AddFactory(&factory);
        foundation::resource::Proxy<terrain::SplatWeights> cooked =
            manager.Bind<terrain::SplatWeights>(id);
        REQUIRE(cooked);
        CHECK(cooked->WeightOfLayer(16, 16, 1) == painted);
    }
    FileDelete(u8"scratch_splat_persist_cooked/splat.rasset");
    FileDelete(u8"scratch_splat_persist_cooked/splat.pixels.bin");
    FileDelete(u8"scratch_splat_persist_cooked/splat.indices.bin");
    RemoveDirectory(cookedDir);

    FileDelete(u8"scratch_splat_persist_db/splat.rasset");
    FileDelete(u8"scratch_splat_persist_db/splat.pixels.bin");
    FileDelete(u8"scratch_splat_persist_db/splat.indices.bin");
    RemoveDirectory(dbDir);
}
