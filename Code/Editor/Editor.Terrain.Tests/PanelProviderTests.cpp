// Terrain tool-panel providers (the first consumers of the editor.app:tool_panel seam): after
// RegisterTerrainToolPanels, the registry resolves a provider for each brush tool id, and each
// provider builds a non-null settings panel for its concrete tool. Headless - the panel is just a
// view tree (no live UI context needed to construct it).
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import foundation.scene;
import foundation.heightfield;
import foundation.terrain.resource;
import engine.terrain;
import editor.core;
import editor.viewporttools;
import editor.app;
import editor.terrain;

using namespace foundation::core;
namespace scene = foundation::scene;
namespace terrain = foundation::terrain;
namespace hf = foundation::heightfield;

TEST_CASE("terrain tool panels: providers register + build a panel for each brush tool")
{
    editor::RegisterTerrainToolPanels(); // idempotent (first-wins per tool id)

    editor::ViewportToolPanelRegistry& reg = editor::ViewportToolPanelRegistry::Get();
    editor::IViewportToolPanelProvider* sculptP = reg.FindByToolId(u8"terrain.sculpt");
    editor::IViewportToolPanelProvider* splatP = reg.FindByToolId(u8"terrain.splat");
    REQUIRE(sculptP != nullptr);
    REQUIRE(splatP != nullptr);
    CHECK(reg.FindByToolId(u8"select") == nullptr); // the default tool has no panel

    // Build the tools the panels drive (in-memory terrain, like the gesture tests).
    scene::Scene sceneObj;
    engine::terrain::AddTerrainSceneManagers(sceneObj);
    editor::EditorCommandStack commands;
    editor::TerrainSculptTool sculpt(sceneObj, commands, nullptr);
    editor::TerrainSplatTool splat(sceneObj, commands, nullptr);

    editor::ViewportToolHostContext ctx;
    ctx.scene = &sceneObj;
    ctx.commands = &commands;

    RefPtr<foundation::ui::View> sculptPanel = sculptP->CreatePanel(sculpt, ctx);
    RefPtr<foundation::ui::View> splatPanel = splatP->CreatePanel(splat, ctx);
    CHECK(sculptPanel.Get() != nullptr);
    CHECK(splatPanel.Get() != nullptr);
}
