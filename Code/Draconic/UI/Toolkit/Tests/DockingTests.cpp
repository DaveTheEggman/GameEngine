// Smoke test for the toolkit docking subsystem. Constructs the docking types and exercises the public
// tree operations (no input simulation, no drawing, no PopupLayer/Context-required paths). The verbatim
// upstream DockingTests/DockLayoutTests/DockPersistenceTests are a later step.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
import draconic.ui.toolkit;

using namespace draconic::ui;
using namespace draconic::ui::toolkit;
using namespace draconic::core;
namespace core = draconic::core;

TEST_CASE("toolkit-docking: DockManager panel + root transitions")
{
    auto dm = core::MakeRef<DockManager>(core::DefaultAllocator());
    CHECK(dm->RootNode() == nullptr);

    DockablePanel* a = dm->AddPanel(u8"A", nullptr);
    DockablePanel* b = dm->AddPanel(u8"B", nullptr);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    // Dock A at Center -> root becomes a (non-null) tab group.
    dm->DockPanel(a, DockPosition::Center);
    CHECK(dm->RootNode() != nullptr);

    // Dock B at Right -> root becomes a split.
    dm->DockPanel(b, DockPosition::Right);
    REQUIRE(dm->RootNode() != nullptr);
    CHECK(Cast<DockSplit>(dm->RootNode()) != nullptr);
}

TEST_CASE("toolkit-docking: DockSplit children via SetChildren")
{
    auto split = core::MakeRef<DockSplit>(core::DefaultAllocator());
    CHECK(split->First() == nullptr);
    CHECK(split->Second() == nullptr);

    auto first = core::MakeRef<DockablePanel>(core::DefaultAllocator(), StringView(u8"First"));
    auto second = core::MakeRef<DockablePanel>(core::DefaultAllocator(), StringView(u8"Second"));
    split->SetChildren(first.Get(), second.Get());

    CHECK(split->First() == first.Get());
    CHECK(split->Second() == second.Get());

    split->SetSplitRatio(0.25f);
    CHECK(split->SplitRatio() == doctest::Approx(0.25f));
}

TEST_CASE("toolkit-docking: DockTabGroup AddPanel + selection")
{
    auto group = core::MakeRef<DockTabGroup>(core::DefaultAllocator());
    CHECK(group->PanelCount() == 0);
    CHECK(group->SelectedIndex() == -1);

    auto p0 = core::MakeRef<DockablePanel>(core::DefaultAllocator(), StringView(u8"P0"));
    auto p1 = core::MakeRef<DockablePanel>(core::DefaultAllocator(), StringView(u8"P1"));
    group->AddPanel(p0.Get());
    group->AddPanel(p1.Get());

    CHECK(group->PanelCount() == 2);
    CHECK(group->SelectedIndex() == 0);       // first added auto-selects
    CHECK(group->SelectedPanel() == p0.Get());

    group->SetSelectedIndex(1);
    CHECK(group->SelectedIndex() == 1);
    CHECK(group->SelectedPanel() == p1.Get());

    CHECK(group->RemovePanel(p0.Get()) == p0.Get());
    CHECK(group->PanelCount() == 1);
}

TEST_CASE("toolkit-docking: DockZoneIndicator targets + hover")
{
    auto zi = core::MakeRef<DockZoneIndicator>(core::DefaultAllocator());
    CHECK(zi->TargetCount() == 0);
    CHECK(zi->HoveredIndex() == -1);
    CHECK_FALSE(zi->HoveredTarget().HasValue());

    zi->AddTarget(DockPosition::Left, Rectangle{ 0, 0, 40, 40 }, nullptr);
    zi->AddTarget(DockPosition::Right, Rectangle{ 100, 0, 40, 40 }, nullptr);
    CHECK(zi->TargetCount() == 2);

    zi->UpdateHover(110, 10);
    CHECK(zi->HoveredIndex() == 1);
    REQUIRE(zi->HoveredTarget().HasValue());
    CHECK(zi->HoveredTarget().Value().Position == DockPosition::Right);

    zi->UpdateHover(500, 500);
    CHECK(zi->HoveredIndex() == -1);

    zi->ClearTargets();
    CHECK(zi->TargetCount() == 0);
}
