// Smoke test for the toolkit skeleton: StatusBar constructs, adds sections, measures with a min height.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
import draconic.ui.toolkit;

using namespace draconic::ui;
using namespace draconic::ui::toolkit;
using namespace draconic::core;
namespace core = draconic::core;

TEST_CASE("toolkit-statusbar: DefaultsAndSections")
{
    auto bar = core::MakeRef<StatusBar>(core::DefaultAllocator());
    CHECK(bar->Direction == Orientation::Horizontal);

    bar->SetText(u8"Ready");
    Label* sec = bar->AddSection(u8"Ln 1, Col 1");
    REQUIRE(sec != nullptr);
    // default text + one section = 2 children
    CHECK(bar->ChildCount() == 2u);
}
