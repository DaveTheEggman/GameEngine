// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Viewport tool-PANEL seam tests. The registry (keyed by tool id,
// idempotent, first-wins) and ViewportToolPanelHost (the active-tool -> panel mount controller) are
// pure logic, so they exercise headlessly: fake tools drive a ViewportToolManager, fake providers
// build throwaway views, and mount/clear callbacks record what the host asked the page to dock.
// Covers the H1 acceptance list: panel appears/disappears with the tool, an unknown/panel-less tool
// docks nothing and never crashes, and gesture-safe switching between two paneled tools clears the
// old panel before mounting the new one.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import editor.core;
import editor.app;
import editor.viewporttools;

using namespace foundation::core;
using namespace editor;

namespace ui = foundation::ui;

namespace
{
    // A minimal viewport tool that only carries an id (the panel seam keys on Id() alone).
    class FakeTool final : public IViewportTool
    {
    public:
        explicit FakeTool(StringView id) : m_id(id) {}
        [[nodiscard]] StringView Id() const override { return m_id.AsView(); }
        [[nodiscard]] StringView DisplayName() const override { return m_id.AsView(); }
        bool Update(const ViewportToolInput&) override { return false; }

    private:
        String m_id;
    };

    // A panel provider for one tool id. Counts CreatePanel calls; can be told to yield no view
    // (the "this context has nothing to show" path).
    class FakeProvider final : public IViewportToolPanelProvider
    {
    public:
        FakeProvider(StringView toolId, bool yieldNull = false) : m_id(toolId), m_null(yieldNull) {}
        [[nodiscard]] StringView ToolId() const override { return m_id.AsView(); }
        [[nodiscard]] ToolPanelPlacement Placement() const override { return placement; }
        ToolPanelPlacement placement = ToolPanelPlacement::Dock; // the host must forward this to mount
        [[nodiscard]] RefPtr<ui::View> CreatePanel(IViewportTool&,
                                                   const ViewportToolHostContext&) override
        {
            ++CreateCount;
            if (m_null)
            {
                return {};
            }
            return MakeRef<ui::FlexLayout>(DefaultAllocator());
        }
        int CreateCount = 0;

    private:
        String m_id;
        bool m_null;
    };

    void AddTool(ViewportToolManager& tools, StringView id)
    {
        tools.Add(MakeUnique<FakeTool>(DefaultAllocator(), id));
    }
}

TEST_CASE("tool-panel registry: idempotent, first-wins per tool id")
{
    ViewportToolPanelRegistry registry;
    FakeProvider anim(u8"anim");
    FakeProvider animAgain(u8"anim");
    FakeProvider terrain(u8"terrain");

    registry.Register(&anim);
    registry.Register(&anim); // same pointer -> ignored
    CHECK(registry.Count() == 1);

    registry.Register(&animAgain); // second provider for "anim" -> ignored (first wins)
    CHECK(registry.Count() == 1);
    CHECK(registry.FindByToolId(u8"anim") == &anim);

    registry.Register(&terrain);
    CHECK(registry.Count() == 2);
    CHECK(registry.FindByToolId(u8"terrain") == &terrain);
    CHECK(registry.FindByToolId(u8"nope") == nullptr);
    registry.Register(nullptr); // tolerated
    CHECK(registry.Count() == 2);
}

TEST_CASE("tool-panel host: panel appears and disappears with its tool")
{
    ViewportToolManager tools;
    AddTool(tools, u8"select"); // first added = default, no panel
    AddTool(tools, u8"anim");

    ViewportToolPanelRegistry registry;
    FakeProvider animPanel(u8"anim");
    registry.Register(&animPanel);

    int mounts = 0;
    int clears = 0;
    ui::View* lastMounted = nullptr;
    ViewportToolHostContext ctx;
    ViewportToolPanelHost host(tools, registry, Move(ctx),
                               [&](ui::View* v, ToolPanelPlacement)
                               {
                                   ++mounts;
                                   lastMounted = v;
                               },
                               [&](ToolPanelPlacement)
                               {
                                   ++clears;
                                   lastMounted = nullptr;
                               });

    // Default tool active: it has no panel, so the first Sync docks nothing.
    host.Sync();
    CHECK(host.CurrentToolId() == StringView(u8"select"));
    CHECK(host.CurrentPanel() == nullptr);
    CHECK(mounts == 0);

    // Switch to the paneled tool: the panel mounts.
    CHECK(tools.ActivateById(u8"anim"));
    host.Sync();
    CHECK(mounts == 1);
    CHECK(clears == 0); // nothing to clear - the tool we left had no panel
    CHECK(host.CurrentPanel() != nullptr);
    CHECK(lastMounted == host.CurrentPanel());
    CHECK(animPanel.CreateCount == 1);

    // A Sync with no active-tool change is a no-op (the view is not rebuilt every frame).
    host.Sync();
    CHECK(mounts == 1);
    CHECK(animPanel.CreateCount == 1);

    // Back to the panel-less default: the panel is torn down.
    tools.ActivateDefault();
    host.Sync();
    CHECK(clears == 1);
    CHECK(host.CurrentPanel() == nullptr);
    CHECK(mounts == 1);
}

TEST_CASE("tool-panel host: unknown / panel-less tool docks nothing")
{
    ViewportToolManager tools;
    AddTool(tools, u8"select");
    AddTool(tools, u8"anim");

    ViewportToolPanelRegistry registry; // no providers registered at all

    int mounts = 0;
    int clears = 0;
    ViewportToolHostContext ctx;
    ViewportToolPanelHost host(tools, registry, Move(ctx), [&](ui::View*, ToolPanelPlacement) { ++mounts; },
                               [&](ToolPanelPlacement) { ++clears; });

    CHECK(tools.ActivateById(u8"anim"));
    host.Sync(); // no provider for "anim" -> no panel, no crash
    CHECK(mounts == 0);
    CHECK(clears == 0);
    CHECK(host.CurrentPanel() == nullptr);
    CHECK(host.CurrentToolId() == StringView(u8"anim"));
}

TEST_CASE("tool-panel host: provider yielding no view mounts nothing")
{
    ViewportToolManager tools;
    AddTool(tools, u8"select");
    AddTool(tools, u8"anim");

    ViewportToolPanelRegistry registry;
    FakeProvider emptyPanel(u8"anim", /*yieldNull=*/true);
    registry.Register(&emptyPanel);

    int mounts = 0;
    int clears = 0;
    ViewportToolHostContext ctx;
    ViewportToolPanelHost host(tools, registry, Move(ctx), [&](ui::View*, ToolPanelPlacement) { ++mounts; },
                               [&](ToolPanelPlacement) { ++clears; });

    CHECK(tools.ActivateById(u8"anim"));
    host.Sync();
    CHECK(emptyPanel.CreateCount == 1); // asked...
    CHECK(mounts == 0);                 // ...but nothing to mount
    CHECK(host.CurrentPanel() == nullptr);

    tools.ActivateDefault();
    host.Sync();
    CHECK(clears == 0); // nothing was mounted, so nothing to clear
}

TEST_CASE("tool-panel host: switching between two paneled tools clears then mounts")
{
    ViewportToolManager tools;
    AddTool(tools, u8"select");
    AddTool(tools, u8"a");
    AddTool(tools, u8"b");

    ViewportToolPanelRegistry registry;
    FakeProvider panelA(u8"a");
    FakeProvider panelB(u8"b");
    registry.Register(&panelA);
    registry.Register(&panelB);

    int mounts = 0;
    int clears = 0;
    ViewportToolHostContext ctx;
    ViewportToolPanelHost host(tools, registry, Move(ctx), [&](ui::View*, ToolPanelPlacement) { ++mounts; },
                               [&](ToolPanelPlacement) { ++clears; });

    CHECK(tools.ActivateById(u8"a"));
    host.Sync();
    CHECK(mounts == 1);
    CHECK(clears == 0);

    CHECK(tools.ActivateById(u8"b"));
    host.Sync();
    CHECK(clears == 1); // a's panel torn down first...
    CHECK(mounts == 2); // ...then b's mounted
    CHECK(host.CurrentToolId() == StringView(u8"b"));
    CHECK(panelA.CreateCount == 1);
    CHECK(panelB.CreateCount == 1);
}

TEST_CASE("tool-panel host: forwards the provider's placement hint to mount + clear")
{
    ViewportToolManager tools;
    AddTool(tools, u8"select");
    AddTool(tools, u8"anim");

    ViewportToolPanelRegistry registry;
    FakeProvider animPanel(u8"anim");
    animPanel.placement = ToolPanelPlacement::ViewportOverlay; // provider wants an overlay
    registry.Register(&animPanel);

    ToolPanelPlacement mountedAt = ToolPanelPlacement::Dock;
    ToolPanelPlacement clearedAt = ToolPanelPlacement::Dock;
    int mounts = 0, clears = 0;
    ViewportToolHostContext ctx;
    ViewportToolPanelHost host(tools, registry, Move(ctx),
                               [&](ui::View*, ToolPanelPlacement p) { ++mounts; mountedAt = p; },
                               [&](ToolPanelPlacement p) { ++clears; clearedAt = p; });

    CHECK(tools.ActivateById(u8"anim"));
    host.Sync();
    CHECK(mounts == 1);
    CHECK(mountedAt == ToolPanelPlacement::ViewportOverlay); // the hint reached the host's mount

    // Leaving the tool tears down at the SAME placement the panel was mounted to.
    tools.ActivateDefault();
    host.Sync();
    CHECK(clears == 1);
    CHECK(clearedAt == ToolPanelPlacement::ViewportOverlay);
}

TEST_CASE("import-plan view: edits the external plan in place - checks live, names on sync")
{
    pipeline::ImportPlan plan;
    const auto add = [&plan](pipeline::ImportResourceKind kind, StringView name)
    {
        pipeline::ImportPlanEntry e;
        e.kind = kind;
        e.sourceName = String(name);
        e.targetName = String(name);
        plan.entries.PushBack(Move(e));
    };
    add(pipeline::ImportResourceKind::Mesh, u8"body");
    add(pipeline::ImportResourceKind::AnimationClip, u8"walk");
    add(pipeline::ImportResourceKind::AnimationClip, u8"run");

    auto view = MakeRef<editor::app::ImportPlanView>(DefaultAllocator(), &plan);

    // Rows: per-entry enable checkboxes write straight into the plan; the section
    // check-all drives every row of its kind. Walk the tree for the checkboxes:
    // [Meshes header][mesh row][Clips header][clip row][clip row].
    Array<ui::CheckBox*> checks;
    const auto collect = [&](ui::View* v, auto&& recurse) -> void
    {
        if (auto* c = Cast<ui::CheckBox>(v))
        {
            checks.PushBack(c);
        }
        if (auto* g = Cast<ui::ViewGroup>(v))
        {
            for (usize i = 0; i < g->ChildCount(); ++i)
            {
                recurse(g->GetChildAt(i), recurse);
            }
        }
    };
    collect(view.Get(), collect);
    REQUIRE(checks.Size() == 5u); // 2 section headers + 3 entry rows

    // The clips section header is the 3rd checkbox; unchecking it disables BOTH clips
    // but not the mesh.
    checks[2]->IsChecked.SetValue(false);
    CHECK(plan.entries[0].enabled);
    CHECK(!plan.entries[1].enabled);
    CHECK(!plan.entries[2].enabled);

    // Rename through the row editor; SyncNames writes it back into the plan.
    Array<ui::EditText*> editors;
    const auto collectEdits = [&](ui::View* v, auto&& recurse) -> void
    {
        if (auto* e = Cast<ui::EditText>(v))
        {
            editors.PushBack(e);
        }
        if (auto* g = Cast<ui::ViewGroup>(v))
        {
            for (usize i = 0; i < g->ChildCount(); ++i)
            {
                recurse(g->GetChildAt(i), recurse);
            }
        }
    };
    collectEdits(view.Get(), collectEdits);
    REQUIRE(editors.Size() == 3u);
    editors[0]->SetText(u8"hero_body");
    view->SyncNames();
    CHECK(plan.entries[0].targetName == u8"hero_body");
    CHECK(plan.entries[1].targetName == u8"walk"); // untouched rows keep their names
}
