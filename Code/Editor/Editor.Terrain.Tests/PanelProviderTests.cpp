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

    // Build the tools the panels drive - over an in-memory terrain with a SIX-layer palette +
    // weights, so the splat panel's base + unbounded-swatch path actually runs past the old
    // 4-layer cap (top-K model).
    scene::Scene sceneObj;
    engine::terrain::AddTerrainSceneManagers(sceneObj);
    auto* mgr = sceneObj.GetSystem<engine::terrain::TerrainComponentManager>();
    RefPtr<hf::Heightfield> grid =
        MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
    RefPtr<terrain::SplatWeights> weights =
        MakeRef<terrain::SplatWeights>(DefaultAllocator(), 16, 16);
    auto res = MakeRef<terrain::TerrainResource>(DefaultAllocator());
    res->heightfield = grid.Get();
    res->weights = weights.Get();
    for (i32 i = 0; i < 6; ++i)
    {
        res->palette.PushBack(terrain::TerrainResource::Layer{});
    }
    mgr->Add(sceneObj.CreateEntity(u8"terrain")).terrain = res.Get();
    sceneObj.Start();

    editor::EditorCommandStack commands;
    editor::TerrainSculptTool sculpt(sceneObj, commands, nullptr);
    editor::TerrainSplatTool splat(sceneObj, commands, nullptr);
    splat.SetPaletteIndex(5); // a slot past the retired 4-layer cap
    CHECK(splat.PaletteIndex() == 5u);

    editor::ViewportToolHostContext ctx;
    ctx.scene = &sceneObj;
    ctx.commands = &commands;

    RefPtr<foundation::ui::View> sculptPanel = sculptP->CreatePanel(sculpt, ctx);
    RefPtr<foundation::ui::View> splatPanel = splatP->CreatePanel(splat, ctx);
    CHECK(sculptPanel.Get() != nullptr);
    CHECK(splatPanel.Get() != nullptr); // built the base swatch + 6 palette slots + eraser

    // Every layer slot carries a NAME tooltip (a displacement thumbnail is indistinguishable
    // from its diffuse sibling at swatch size - the name is the disambiguator). Headless with no
    // editor context the names resolve to placeholders, but they must be present and non-empty.
    i32 tooltipped = 0;
    const auto countTooltips = [&](foundation::ui::View& v, const auto& recurse) -> void
    {
        if (!v.TooltipText.IsEmpty())
        {
            ++tooltipped;
        }
        if (auto* group = Cast<foundation::ui::ViewGroup>(&v))
        {
            for (usize k = 0; k < group->ChildCount(); ++k)
            {
                if (foundation::ui::View* child = group->GetChildAt(k))
                {
                    recurse(*child, recurse);
                }
            }
        }
    };
    countTooltips(*splatPanel, countTooltips);
    CHECK(tooltipped >= 7); // 6 palette slots + the eraser (+ the base swatch when thumbs exist)

    // The eraser mode the panel's last slot drives round-trips on the tool.
    splat.SetEraser(true);
    CHECK(splat.IsEraser());
    splat.SetEraser(false);
    CHECK(splat.PaletteIndex() == 5u); // selection survives leaving eraser mode
}
