// TerrainSculptTool tests (headless): a whole sculpt gesture is scriptable through ViewportToolInput
// (no viewport), so this drives press -> drag -> release over an in-memory terrain and asserts the
// shared heightfield is raised, ONE region-delta command undoes/redoes it, the brush is unavailable
// with no terrain + refuses edits while editingLocked, and the write-back-to-source closure is
// registered on the asset-edit sink with the heightfield's source guid.

#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.content; // ContentDatabase (the IAssetEditSink persist signature)
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
        }

        i32 count = 0;
        Guid lastId;
        bool hasPersist = false;
    };

    // A straight-down ray at the terrain centre (identity entity transform -> local == world).
    editor::ViewportToolInput CentreRay(f32 deltaSeconds)
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
    editor::ViewportToolInput press = CentreRay(0.1f);
    press.leftPressed = true;
    press.leftDown = true;
    CHECK(tool.Update(press)); // consumed: the brush owns the click

    const hf::Height afterDab = fx.grid->GetSample(32, 32);
    CHECK(afterDab > before);
    CHECK(fx.grid->Version() > 1u); // sculpt bumped the version (GPU re-upload)

    // A held drag frame raises further.
    editor::ViewportToolInput drag = CentreRay(0.1f);
    drag.leftDown = true;
    CHECK(tool.Update(drag));
    CHECK(fx.grid->GetSample(32, 32) > afterDab);

    // Release: commits ONE command + registers the persist closure.
    editor::ViewportToolInput release = CentreRay(0.1f);
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

    editor::ViewportToolInput press = CentreRay(0.2f);
    press.leftPressed = true;
    press.leftDown = true;
    (void)tool.Update(press);
    editor::ViewportToolInput release = CentreRay(0.2f);
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
    scene::Scene empty;
    engine::terrain::AddTerrainSceneManagers(empty);
    empty.Start();
    editor::EditorCommandStack commandsA;
    editor::TerrainSculptTool bare(empty, commandsA, nullptr);
    CHECK_FALSE(bare.IsAvailable());

    // With terrain but editingLocked (Simulate): a press does nothing (shared collider).
    Fixture fx;
    editor::EditorCommandStack commands;
    editor::TerrainSculptTool tool(fx.scene, commands, nullptr);
    editor::ViewportToolInput locked = CentreRay(0.1f);
    locked.leftPressed = true;
    locked.leftDown = true;
    locked.editingLocked = true;
    (void)tool.Update(locked);
    CHECK(fx.grid->GetSample(32, 32) == 0); // no edit under lock
    CHECK_FALSE(commands.CanUndo());
}
