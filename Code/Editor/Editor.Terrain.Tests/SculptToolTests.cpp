// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// TerrainSculptTool tests (headless): a whole sculpt gesture is scriptable through ViewportToolInput
// (no viewport), so this drives press -> drag -> release over an in-memory terrain and asserts the
// shared heightfield is raised, ONE region-delta command undoes/redoes it, the brush is unavailable
// with no terrain + refuses edits while editingLocked, and the write-back-to-source closure is
// registered on the asset-edit sink with the heightfield's source guid.

#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.content; // ContentDatabase (the IAssetEditSink persist signature)
import foundation.vfs;    // NativeFileSystem (the persist round-trip test)
import foundation.resource;
import foundation.scene;
import foundation.heightfield;
import foundation.heightfield.resource; // HeightfieldFactory + kHeightStream (re-cook survival)
import foundation.terrain.resource;
import engine.terrain;
import pipeline.core;
import heightfield.pipeline; // HeightfieldAsset + builder (the SOURCE envelope the persist rewrites)
import editor.core;
import editor.viewporttools;
import editor.terrain;

using namespace foundation::core;
namespace scene = foundation::scene;
namespace hf = foundation::heightfield;
namespace terrain = foundation::terrain;

namespace
{
    // Captures the last registered (guid, persist) pair so a test can assert registration + run it.
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

    // A straight-down ray at the terrain center (identity entity transform -> local == world).
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
        scene::Scene scene{DefaultAllocator()};
        RefPtr<hf::Heightfield> grid;
        RefPtr<terrain::TerrainResource> res;

        Fixture()
        {
            engine::terrain::AddTerrainSceneManagers(scene);
            auto* mgr = scene.GetSystem<engine::terrain::TerrainComponentManager>();
            grid = MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
            res = MakeRef<terrain::TerrainResource>(DefaultAllocator());
            res->heightfield = grid.Get();
            res->heightfield.SetId(Guid{7, 99}); // the source asset guid the tool persists back to
            const scene::EntityHandle e = scene.CreateEntity(u8"terrain");
            engine::terrain::TerrainComponent& c = mgr->Add(e);
            c.terrain = res.Get();
            scene.Start();
        }
    };
}

TEST_CASE("terrain sculpt: a press-drag-release stroke raises the shared heightfield")
{
    Fixture fx;
    editor::EditorCommandStack commands;
    FakeAssetEditSink sink;
    editor::TerrainSculptTool tool(fx.scene, commands, &sink);

    CHECK(tool.IsAvailable());
    const hf::Height before = fx.grid->GetSample(32, 32);
    CHECK(before == 0);

    // Press (begins the stroke + deposits the first dab).
    editor::ViewportToolInput press = CenterRay(0.1f);
    press.leftPressed = true;
    press.leftDown = true;
    CHECK(tool.Update(press)); // consumed: the brush owns the click

    const hf::Height afterDab = fx.grid->GetSample(32, 32);
    CHECK(afterDab > before);
    CHECK(fx.grid->Version() > 1u); // sculpt bumped the version (GPU re-upload)

    // A held drag frame raises further.
    editor::ViewportToolInput drag = CenterRay(0.1f);
    drag.leftDown = true;
    CHECK(tool.Update(drag));
    CHECK(fx.grid->GetSample(32, 32) > afterDab);

    // Release: commits ONE command + registers the persist closure.
    editor::ViewportToolInput release = CenterRay(0.1f);
    release.leftReleased = true;
    (void)tool.Update(release);

    CHECK(commands.CanUndo());
    CHECK(sink.count == 1);
    CHECK(sink.lastId == Guid{7, 99});
    CHECK(sink.hasPersist);
}

TEST_CASE("terrain sculpt: one command per stroke undoes/redoes the whole region")
{
    Fixture fx;
    editor::EditorCommandStack commands;
    FakeAssetEditSink sink;
    editor::TerrainSculptTool tool(fx.scene, commands, &sink);

    editor::ViewportToolInput press = CenterRay(0.2f);
    press.leftPressed = true;
    press.leftDown = true;
    (void)tool.Update(press);
    editor::ViewportToolInput release = CenterRay(0.2f);
    release.leftReleased = true;
    (void)tool.Update(release);

    const hf::Height raised = fx.grid->GetSample(32, 32);
    REQUIRE(raised > 0);
    const u64 versionAfterStroke = fx.grid->Version();

    commands.Undo();
    CHECK(fx.grid->GetSample(32, 32) == 0);          // region restored to the before-state
    CHECK(fx.grid->Version() > versionAfterStroke);  // undo bumps the version too

    commands.Redo();
    CHECK(fx.grid->GetSample(32, 32) == raised);     // redo replays the after-state
}

TEST_CASE("terrain sculpt: unavailable with no terrain, and refuses edits while editingLocked")
{
    // No terrain in the scene: the tool is not relevant.
    scene::Scene empty{DefaultAllocator()};
    engine::terrain::AddTerrainSceneManagers(empty);
    empty.Start();
    editor::EditorCommandStack commandsA;
    editor::TerrainSculptTool bare(empty, commandsA, nullptr);
    CHECK_FALSE(bare.IsAvailable());

    // With terrain but editingLocked (Simulate): a press does nothing (shared collider).
    Fixture fx;
    editor::EditorCommandStack commands;
    editor::TerrainSculptTool tool(fx.scene, commands, nullptr);
    editor::ViewportToolInput locked = CenterRay(0.1f);
    locked.leftPressed = true;
    locked.leftDown = true;
    locked.editingLocked = true;
    (void)tool.Update(locked);
    CHECK(fx.grid->GetSample(32, 32) == 0); // no edit under lock
    CHECK_FALSE(commands.CanUndo());
}

// The persist closure must write the SOURCE HeightfieldAsset envelope (never the
// cooked HeightfieldSource type - that clobbered the source asset), clear fileName (the authored
// "heights" sidecar becomes the truth), and the sculpt must SURVIVE a re-cook through the builder's
// embedded path.
TEST_CASE("terrain sculpt: a save persists to the source asset and survives a re-cook")
{
    using foundation::vfs::NativeFileSystem;
    namespace content = foundation::content;

    const StringView dbDir = u8"scratch_sculpt_persist_db";
    FileDelete(u8"scratch_sculpt_persist_db/hf.rasset");
    FileDelete(u8"scratch_sculpt_persist_db/hf.heights.bin");
    RemoveDirectory(dbDir);
    NativeFileSystem mount(dbDir, DefaultAllocator());

    // The SOURCE asset: an imported-style envelope (fileName set) proving the save converts it.
    Guid id;
    {
        content::ContentDatabase db(foundation::core::DefaultAllocator(), mount, foundation::core::BinarySerializerFactory(),
                                    u8".rasset");
        pipeline::RegisterHeightfieldAsset();
        foundation::heightfield::RegisterHeightfieldResourceTypes();
        auto* inst =
            db.RootGroup()->CreateInstance(u8"hf", pipeline::HeightfieldAsset::StaticType());
        REQUIRE(inst != nullptr);
        id = inst->Id();
        pipeline::HeightfieldAsset src;
        src.size = 65;
        src.worldSize = Float2{64.0f, 64.0f};
        src.minY = 0.0f;
        src.maxY = 10.0f;
        src.fileName = foundation::vfs::SourcePath(u8"legacy.png"); // imported-style
        REQUIRE(inst->WriteObject(src).IsOk());
    }

    // Sculpt a stroke against the shared runtime grid bound to that source id.
    Fixture fx;
    fx.res->heightfield.SetId(id);
    FakeAssetEditSink sink;
    editor::EditorCommandStack commands;
    editor::TerrainSculptTool tool(fx.scene, commands, &sink);

    editor::ViewportToolInput press = CenterRay(0.1f);
    press.leftPressed = true;
    press.leftDown = true;
    (void)tool.Update(press);
    editor::ViewportToolInput release = CenterRay(0.0f);
    release.leftReleased = true;
    (void)tool.Update(release);
    REQUIRE(sink.lastPersist);
    CHECK(sink.lastId == id);
    const hf::Height sculpted = fx.grid->GetSample(32, 32);
    REQUIRE(sculpted > 0); // the stroke raised the grid

    // Drain: the closure writes envelope + sidecar into the SOURCE DB, then re-cook into a
    // SEPARATE cooked DB under the SAME guid (product guid == source guid - the production shape;
    // the cooked instance carries the product-source type the factory reads).
    const StringView cookedDir = u8"scratch_sculpt_persist_cooked";
    FileDelete(u8"scratch_sculpt_persist_cooked/hf.rasset");
    FileDelete(u8"scratch_sculpt_persist_cooked/hf.heights.bin");
    FileDelete(u8"scratch_sculpt_persist_cooked/hf.holes.bin");
    RemoveDirectory(cookedDir);
    NativeFileSystem cookedMount(cookedDir, DefaultAllocator());
    {
        content::ContentDatabase db(foundation::core::DefaultAllocator(), mount, foundation::core::BinarySerializerFactory(),
                                    u8".rasset");
        REQUIRE(sink.lastPersist(db).IsOk());
        content::Instance* inst = db.GetInstance(id);
        REQUIRE(inst != nullptr);
        RefPtr<ISerializable> object = inst->ReadObject();
        auto* asset = Cast<pipeline::HeightfieldAsset>(object.Get());
        REQUIRE(asset != nullptr);               // envelope is STILL a HeightfieldAsset
        CHECK(asset->fileName.View().IsEmpty()); // converted to embedded (sidecar = truth)
        CHECK(asset->size == 65);

        content::ContentDatabase cookedDb(foundation::core::DefaultAllocator(), cookedMount,
                                          foundation::core::BinarySerializerFactory(),
                                          u8".rasset");
        content::Instance* cookedInst = cookedDb.RootGroup()->CreateInstanceWithId(
            id, u8"hf", foundation::heightfield::HeightfieldSource::StaticType());
        REQUIRE(cookedInst != nullptr);

        pipeline::HeightfieldAssetBuilder builder;
        NativeFileSystem srcMount(u8".", DefaultAllocator());
        pipeline::AssetBuildContext ctx{editor::EditorRootAllocator()};
        ctx.sources = &srcMount;
        ctx.source = inst;
        ctx.output = cookedInst;
        REQUIRE(builder.Build(*asset, ctx).IsOk());
    }

    // Bind the cooked product through a FRESH db: the sculpt survived the re-cook.
    {
        content::ContentDatabase cookedDb(foundation::core::DefaultAllocator(), cookedMount,
                                          foundation::core::BinarySerializerFactory(),
                                          u8".rasset");
        foundation::heightfield::HeightfieldFactory factory(DefaultAllocator());
        foundation::resource::ResourceManager manager(foundation::core::DefaultAllocator(), cookedDb);
        manager.AddFactory(&factory);
        foundation::resource::Proxy<hf::Heightfield> cooked = manager.Bind<hf::Heightfield>(id);
        REQUIRE(cooked);
        CHECK(cooked->GetSample(32, 32) == sculpted);
    }
    FileDelete(u8"scratch_sculpt_persist_cooked/hf.rasset");
    FileDelete(u8"scratch_sculpt_persist_cooked/hf.heights.bin");
    FileDelete(u8"scratch_sculpt_persist_cooked/hf.holes.bin");
    RemoveDirectory(cookedDir);

    FileDelete(u8"scratch_sculpt_persist_db/hf.rasset");
    FileDelete(u8"scratch_sculpt_persist_db/hf.heights.bin");
    RemoveDirectory(dbDir);
}
