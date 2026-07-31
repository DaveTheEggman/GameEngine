// Smoke test for Toolbar: constructs, adds buttons/separators/toggles, toggle round-trips + fires event.
#include <doctest/doctest.h>
#include "Draconic.Core/Prelude.h"
import draconic.core;
import draconic.ui;
import draconic.ui.toolkit;

using namespace draconic::ui;
using namespace draconic::ui::toolkit;
using namespace draconic::core;
namespace core = draconic::core;

TEST_CASE("toolkit-toolbar: AddsItemsAndToggles")
{
    auto bar = core::MakeRef<Toolbar>(core::DefaultAllocator());
    CHECK(bar->Direction == Orientation::Horizontal);

    ToolbarButton* btn = bar->AddButton(u8"File");
    REQUIRE(btn != nullptr);
    ToolbarSeparator* sep = bar->AddSeparator();
    REQUIRE(sep != nullptr);
    ToolbarToggle* toggle = bar->AddToggle(u8"Bold");
    REQUIRE(toggle != nullptr);

    // button + separator + toggle = 3 children
    CHECK(bar->ChildCount() == 3u);

    // Toggle round-trip fires OnCheckedChanged.
    bool fired = false;
    bool lastValue = false;
    toggle->OnCheckedChanged.Add(
        [&](ToolbarToggle*, bool v)
        {
            fired = true;
            lastValue = v;
        });
    CHECK(toggle->IsChecked() == false);
    toggle->SetIsChecked(true);
    CHECK(toggle->IsChecked() == true);
    CHECK(fired);
    CHECK(lastValue == true);

    // Setting to the same value does not re-fire.
    fired = false;
    toggle->SetIsChecked(true);
    CHECK(fired == false);

    // Button OnClick event wiring.
    bool clicked = false;
    btn->OnClick.Add([&](ToolbarButton*) { clicked = true; });
    btn->OnClick.Invoke(btn);
    CHECK(clicked);
}
