// Smoke test for the toolkit ButtonEditor: builds a Button whose click drives the action.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
import draconic.ui.toolkit;

using namespace draconic::ui;
using namespace draconic::ui::toolkit;
using namespace draconic::core;
namespace core = draconic::core;

TEST_CASE("toolkit-buttoneditor: ClickInvokesAction")
{
    int clicks = 0;
    auto ed = core::MakeRef<ButtonEditor>(core::DefaultAllocator(), StringView(u8"Add Condition"),
        Function<void()>{ [&clicks]() { ++clicks; } });

    auto* btn = core::Cast<Button>(ed->EditorView());
    REQUIRE(btn != nullptr);

    btn->OnClick.Invoke(btn);
    CHECK(clicks == 1);

    ed->RefreshView(); // no-op
}
