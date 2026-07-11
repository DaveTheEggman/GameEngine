// Faithful port of Sedulous.UI.Tests/src/DockingTests.bf (12 cases). Beef `scope`/`new` view trees
// become RefPtr-owned views; `===` ref-equality becomes pointer `==` (with .Get()); `Test.Assert` -> CHECK.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
import draconic.ui.toolkit;
using namespace draconic::ui;
using namespace draconic::ui::toolkit;
using namespace draconic::core;
namespace core = draconic::core;

// new Label("Content") -> a RefPtr<Label>; pass .Get() to AddPanel (the panel/tree adopts a ref).
static RefPtr<Label> MakeLabel(StringView text)
{
    return MakeRef<Label>(DefaultAllocator(), text);
}

TEST_CASE("docking: DockablePanel_Title")
{
    auto panel = MakeRef<DockablePanel>(DefaultAllocator(), StringView(u8"My Panel"));
    CHECK(panel->Title() == StringView(u8"My Panel"));

    panel->SetTitle(StringView(u8"Renamed"));
    CHECK(panel->Title() == StringView(u8"Renamed"));
}

TEST_CASE("docking: DockablePanel_SetContent")
{
    UIContext ctx;
    auto root = MakeRef<RootView>(DefaultAllocator());
    ctx.AddRootView(root.Get());
    auto panel = MakeRef<DockablePanel>(DefaultAllocator(), StringView(u8"Test"));
    root->AddView(panel.Get());

    auto content = MakeRef<Label>(DefaultAllocator());
    panel->SetContent(content.Get());
    CHECK(panel->ContentView() == content.Get());
}

TEST_CASE("docking: DockSplit_RatioClamping")
{
    auto split = MakeRef<DockSplit>(DefaultAllocator());
    split->SetSplitRatio(-1);
    CHECK(split->SplitRatio() >= 0.05f);

    split->SetSplitRatio(2);
    CHECK(split->SplitRatio() <= 0.95f);
}

TEST_CASE("docking: DockZoneIndicator_AddTargets")
{
    auto indicator = MakeRef<DockZoneIndicator>(DefaultAllocator());
    CHECK(indicator->TargetCount() == 0);

    indicator->AddTarget(DockPosition::Left, Rectangle{ 0, 0, 100, 400 }, nullptr);
    indicator->AddTarget(DockPosition::Right, Rectangle{ 300, 0, 100, 400 }, nullptr);
    CHECK(indicator->TargetCount() == 2);

    indicator->ClearTargets();
    CHECK(indicator->TargetCount() == 0);
}

TEST_CASE("docking: DockZoneIndicator_UpdateHover")
{
    auto indicator = MakeRef<DockZoneIndicator>(DefaultAllocator());
    indicator->AddTarget(DockPosition::Left, Rectangle{ 0, 0, 100, 400 }, nullptr);
    indicator->AddTarget(DockPosition::Right, Rectangle{ 300, 0, 100, 400 }, nullptr);

    indicator->UpdateHover(50, 200);
    CHECK(indicator->HoveredTarget().HasValue());
    CHECK(indicator->HoveredTarget().Value().Position == DockPosition::Left);

    indicator->UpdateHover(350, 200);
    CHECK(indicator->HoveredTarget().Value().Position == DockPosition::Right);

    indicator->UpdateHover(200, 200);
    CHECK(!indicator->HoveredTarget().HasValue());
}

// === DockTabGroup (standalone, no DockManager) ===

TEST_CASE("docking: DockTabGroup_AddRemovePanel")
{
    auto group = MakeRef<DockTabGroup>(DefaultAllocator());
    auto p1 = MakeRef<DockablePanel>(DefaultAllocator(), StringView(u8"Panel 1"));
    auto p2 = MakeRef<DockablePanel>(DefaultAllocator(), StringView(u8"Panel 2"));

    group->AddPanel(p1.Get());
    CHECK(group->PanelCount() == 1);
    CHECK(group->SelectedIndex() == 0);

    group->AddPanel(p2.Get());
    CHECK(group->PanelCount() == 2);

    group->RemovePanel(p1.Get());
    CHECK(group->PanelCount() == 1);
    // Beef `delete p1` dropped: the local RefPtr frees p1 at scope end.
}

TEST_CASE("docking: DockTabGroup_SelectedIndex")
{
    auto group = MakeRef<DockTabGroup>(DefaultAllocator());
    auto p1 = MakeRef<DockablePanel>(DefaultAllocator(), StringView(u8"A"));
    auto p2 = MakeRef<DockablePanel>(DefaultAllocator(), StringView(u8"B"));

    group->AddPanel(p1.Get());
    group->AddPanel(p2.Get());

    CHECK(group->SelectedIndex() == 0);
    CHECK(group->SelectedPanel() == p1.Get());

    group->SetSelectedIndex(1);
    CHECK(group->SelectedPanel() == p2.Get());
}

// === DockSplit (standalone) ===

TEST_CASE("docking: DockSplit_SetChildren")
{
    auto split = MakeRef<DockSplit>(DefaultAllocator());
    auto child1 = MakeRef<DockTabGroup>(DefaultAllocator());
    auto child2 = MakeRef<DockTabGroup>(DefaultAllocator());

    split->SetChildren(child1.Get(), child2.Get());
    CHECK(split->First() == child1.Get());
    CHECK(split->Second() == child2.Get());
}

// === DockableWindow (standalone) ===

TEST_CASE("docking: DockableWindow_DetachPanel")
{
    auto panel = MakeRef<DockablePanel>(DefaultAllocator(), StringView(u8"Test"));
    auto dw = MakeRef<DockableWindow>(DefaultAllocator(), panel.Get());

    CHECK(dw->Panel() == panel.Get());

    DockablePanel* detached = dw->DetachPanel();
    CHECK(detached == panel.Get());
    CHECK(dw->Panel() == nullptr);
    // Beef `delete panel` dropped: the local RefPtr frees panel at scope end.
}

// === DockManager (with context) ===

TEST_CASE("docking: DockManager_AddPanel")
{
    UIContext ctx;
    auto root = MakeRef<RootView>(DefaultAllocator());
    root->ViewportSize = Float2{ 800, 600 };
    ctx.AddRootView(root.Get());

    auto dm = MakeRef<DockManager>(DefaultAllocator());
    root->AddView(dm.Get());
    ctx.BeginFrame(0.016f);
    root->Measure(BoxConstraints::Tight(800, 600));
    root->Layout(0, 0, 800, 600);

    DockablePanel* panel = dm->AddPanel(StringView(u8"Test"), MakeLabel(u8"Content").Get());
    CHECK(panel != nullptr);
    CHECK(panel->Title() == StringView(u8"Test"));
    CHECK(panel->DockHost == dm.Get());

    // Dock the panel so it's in the view tree and gets cleaned up.
    dm->DockPanel(panel, DockPosition::Center);
}

TEST_CASE("docking: DockManager_DockPanel_Center")
{
    UIContext ctx;
    auto root = MakeRef<RootView>(DefaultAllocator());
    root->ViewportSize = Float2{ 800, 600 };
    ctx.AddRootView(root.Get());

    auto dm = MakeRef<DockManager>(DefaultAllocator());
    root->AddView(dm.Get());
    ctx.BeginFrame(0.016f);
    root->Measure(BoxConstraints::Tight(800, 600));
    root->Layout(0, 0, 800, 600);

    DockablePanel* panel = dm->AddPanel(StringView(u8"P1"), MakeLabel(u8"Content 1").Get());
    dm->DockPanel(panel, DockPosition::Center);

    CHECK(dm->RootNode() != nullptr);
}

TEST_CASE("docking: DockManager_DockPanel_Split")
{
    UIContext ctx;
    auto root = MakeRef<RootView>(DefaultAllocator());
    root->ViewportSize = Float2{ 800, 600 };
    ctx.AddRootView(root.Get());

    auto dm = MakeRef<DockManager>(DefaultAllocator());
    root->AddView(dm.Get());
    ctx.BeginFrame(0.016f);
    root->Measure(BoxConstraints::Tight(800, 600));
    root->Layout(0, 0, 800, 600);

    DockablePanel* p1 = dm->AddPanel(StringView(u8"P1"), MakeLabel(u8"Content 1").Get());
    DockablePanel* p2 = dm->AddPanel(StringView(u8"P2"), MakeLabel(u8"Content 2").Get());

    dm->DockPanel(p1, DockPosition::Center);
    dm->DockPanel(p2, DockPosition::Right);

    CHECK(Cast<DockSplit>(dm->RootNode()) != nullptr);
}

// DELIBERATE DEVIATION from the Sedulous port (user-approved 2026-07-11): docking a panel into a
// tab group makes it the ACTIVE tab (mainstream-IDE behavior). Upstream keeps the existing
// selection and its editor calls ActivatePanel by hand.
TEST_CASE("docking: DockPanel_Center_ActivatesDockedTab")
{
    UIContext ctx;
    auto root = MakeRef<RootView>(DefaultAllocator());
    root->ViewportSize = Float2{ 800, 600 };
    ctx.AddRootView(root.Get());

    auto dm = MakeRef<DockManager>(DefaultAllocator());
    root->AddView(dm.Get());

    DockablePanel* p1 = dm->AddPanel(StringView(u8"P1"), MakeLabel(u8"Content 1").Get());
    DockablePanel* p2 = dm->AddPanel(StringView(u8"P2"), MakeLabel(u8"Content 2").Get());
    DockablePanel* p3 = dm->AddPanel(StringView(u8"P3"), MakeLabel(u8"Content 3").Get());

    dm->DockPanel(p1, DockPosition::Center);
    dm->DockPanel(p2, DockPosition::Center);   // tabs with p1 - and becomes the active tab

    auto* group = Cast<DockTabGroup>(p2->Parent);
    REQUIRE(group != nullptr);
    CHECK(group->PanelCount() == 2);
    CHECK(group->SelectedPanel() == p2);

    // Same through the relative-to path.
    dm->DockPanelRelativeTo(p3, DockPosition::Center, p1->Parent);
    CHECK(group->SelectedPanel() == p3);

    // Explicit activation still works on a background tab.
    dm->ActivatePanel(p1);
    CHECK(group->SelectedPanel() == p1);
}
