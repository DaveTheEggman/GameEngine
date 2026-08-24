// TerrainSplatTool tests (headless): a whole paint gesture scripted through ViewportToolInput. Drives
// press -> drag -> release over an in-memory terrain (heightfield + splatmap) and asserts the selected
// layer channel is raised (and the others eroded - lerp-to-one-hot) in the touched region, ONE
// region-delta command undoes/redoes it, the brush is unavailable with no splatmap + refuses edits
// under Simulate, and the write-back-to-source closure is registered with the splatmap's source guid.

#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.content;
import foundation.scene;
import foundation.heightfield;
import foundation.terrain.resource;
import engine.terrain;
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
        }
        i32 count = 0;
        Guid lastId;
        bool hasPersist = false;
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
        RefPtr<terrain::Splatmap> splat;
        RefPtr<terrain::TerrainResource> res;

        Fixture()
        {
            engine::terrain::AddTerrainSceneManagers(scene);
            auto* mgr = scene.GetSystem<engine::terrain::TerrainComponentManager>();
            grid = MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
            splat = MakeRef<terrain::Splatmap>(DefaultAllocator(), 32, 32);
            splat->SeedLayer0();
            res = MakeRef<terrain::TerrainResource>(DefaultAllocator());
            res->heightfield = grid.Get();
            res->splatmap = splat.Get();
            res->splatmap.SetId(Guid{5, 55}); // the source asset guid the tool persists back to
            const scene::EntityHandle e = scene.CreateEntity(u8"terrain");
            engine::terrain::TerrainComponent& c = mgr->Add(e);
            c.terrain = res.Get();
            scene.Start();
        }
    };
}

TEST_CASE("terrain splat: a press-drag-release stroke paints the selected layer over the base")
{
    Fixture fx;
    editor::EditorCommandStack commands;
    FakeAssetEditSink sink;
    editor::TerrainSplatTool tool(fx.scene, commands, &sink);
    tool.SetLayer(1); // paint layer 1 (green) over the seeded layer 0

    CHECK(tool.IsAvailable());
    CHECK(fx.splat->GetWeight(16, 16, 0) == 255); // base layer full before painting
    CHECK(fx.splat->GetWeight(16, 16, 1) == 0);
    const u64 v0 = fx.splat->Version();

    editor::ViewportToolInput press = CenterRay(0.5f);
    press.leftPressed = true;
    press.leftDown = true;
    CHECK(tool.Update(press)); // consumed: the brush owns the click

    const u8 dab1 = fx.splat->GetWeight(16, 16, 1);
    CHECK(dab1 > 0);                            // layer 1 rose at the centre
    CHECK(fx.splat->GetWeight(16, 16, 0) < 255); // layer 0 eroded (lerp-to-one-hot)
    CHECK(fx.splat->Version() > v0);            // paint bumped the version (GPU re-upload)

    editor::ViewportToolInput drag = CenterRay(0.5f);
    drag.leftDown = true;
    CHECK(tool.Update(drag));
    CHECK(fx.splat->GetWeight(16, 16, 1) > dab1); // more layer 1 after another dab

    editor::ViewportToolInput release = CenterRay(0.5f);
    release.leftReleased = true;
    (void)tool.Update(release);

    CHECK(commands.CanUndo());
    CHECK(sink.count == 1);
    CHECK(sink.lastId == Guid{5, 55});
    CHECK(sink.hasPersist);
}

TEST_CASE("terrain splat: one command per stroke undoes/redoes the whole region")
{
    Fixture fx;
    editor::EditorCommandStack commands;
    FakeAssetEditSink sink;
    editor::TerrainSplatTool tool(fx.scene, commands, &sink);
    tool.SetLayer(2);

    editor::ViewportToolInput press = CenterRay(1.0f);
    press.leftPressed = true;
    press.leftDown = true;
    (void)tool.Update(press);
    editor::ViewportToolInput release = CenterRay(1.0f);
    release.leftReleased = true;
    (void)tool.Update(release);

    const u8 painted = fx.splat->GetWeight(16, 16, 2);
    REQUIRE(painted > 0);

    commands.Undo();
    CHECK(fx.splat->GetWeight(16, 16, 2) == 0);   // region restored to the before-state
    CHECK(fx.splat->GetWeight(16, 16, 0) == 255); // base layer back to full

    commands.Redo();
    CHECK(fx.splat->GetWeight(16, 16, 2) == painted); // redo replays the after-state
}

TEST_CASE("terrain splat: unavailable with no splatmap, and refuses edits while editingLocked")
{
    // A terrain with a heightfield but NO splatmap: the paint tool is not relevant.
    scene::Scene bare;
    engine::terrain::AddTerrainSceneManagers(bare);
    auto* mgr = bare.GetSystem<engine::terrain::TerrainComponentManager>();
    RefPtr<hf::Heightfield> grid =
        MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
    auto res = MakeRef<terrain::TerrainResource>(DefaultAllocator());
    res->heightfield = grid.Get(); // no splatmap
    const scene::EntityHandle e = bare.CreateEntity(u8"terrain");
    mgr->Add(e).terrain = res.Get();
    bare.Start();
    editor::EditorCommandStack commandsA;
    editor::TerrainSplatTool bareTool(bare, commandsA, nullptr);
    CHECK_FALSE(bareTool.IsAvailable());

    // With a splatmap but editingLocked (Simulate): a press does nothing.
    Fixture fx;
    editor::EditorCommandStack commands;
    editor::TerrainSplatTool tool(fx.scene, commands, nullptr);
    tool.SetLayer(1);
    editor::ViewportToolInput locked = CenterRay(0.5f);
    locked.leftPressed = true;
    locked.leftDown = true;
    locked.editingLocked = true;
    (void)tool.Update(locked);
    CHECK(fx.splat->GetWeight(16, 16, 1) == 0); // no edit under lock
    CHECK_FALSE(commands.CanUndo());
}
