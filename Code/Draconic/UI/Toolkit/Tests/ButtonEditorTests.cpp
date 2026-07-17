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

TEST_CASE("toolkit-buttoneditor: SetButtonEnabled applies before and after view creation")
{
    auto editor = core::MakeRef<ButtonEditor>(core::DefaultAllocator(),
        StringView(u8"Revert"), core::Function<void()>{});
    CHECK(editor->ButtonEnabled());

    // Set BEFORE the lazy view exists: the created button starts disabled.
    editor->SetButtonEnabled(false);
    View* view = editor->EditorView();
    REQUIRE(view != nullptr);
    CHECK(!view->IsEnabled);

    // Live toggle on the existing button.
    editor->SetButtonEnabled(true);
    CHECK(view->IsEnabled);
}
