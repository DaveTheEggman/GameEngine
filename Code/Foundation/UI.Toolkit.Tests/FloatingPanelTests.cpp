// FloatingPanel control tests (foundation.ui.toolkit:floating_panel): the draggable / resizable /
// collapsible / closable float panel. Exercised headlessly (no UIContext, no layout pass) - covers
// construction, the collapse toggle, content swap, preferred-size clamping, and OnClose. The drag /
// resize geometry needs a laid-out parent + captured mouse (the in-app path) and is out of scope.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;

using namespace foundation::ui;
using namespace foundation::ui::toolkit;
using namespace foundation::core;
namespace core = foundation::core;

TEST_CASE("FloatingPanel: constructs expanded")
{
    auto panel = core::MakeRef<FloatingPanel>(core::DefaultAllocator(), StringView(u8"Brush"));
    CHECK(panel.Get() != nullptr);
    CHECK_FALSE(panel->IsCollapsed());
}

TEST_CASE("FloatingPanel: collapse toggles and is idempotent")
{
    auto panel = core::MakeRef<FloatingPanel>(core::DefaultAllocator(), StringView(u8"Tools"));
    panel->SetCollapsed(true);
    CHECK(panel->IsCollapsed());
    panel->SetCollapsed(true); // no-op
    CHECK(panel->IsCollapsed());
    panel->SetCollapsed(false);
    CHECK_FALSE(panel->IsCollapsed());
}

TEST_CASE("FloatingPanel: collapse hides the content, expand shows it")
{
    auto panel = core::MakeRef<FloatingPanel>(core::DefaultAllocator(), StringView(u8"P"));
    auto body = core::MakeRef<Label>(core::DefaultAllocator(), StringView(u8"body"));
    panel->SetContent(RefPtr<View>(body.Get()));
    CHECK(body->Visibility == Visibility::Visible);
    panel->SetCollapsed(true);
    CHECK(body->Visibility == Visibility::Gone);
    panel->SetCollapsed(false);
    CHECK(body->Visibility == Visibility::Visible);
}

TEST_CASE("FloatingPanel: content swap and title/size setters are safe")
{
    auto panel = core::MakeRef<FloatingPanel>(core::DefaultAllocator(), StringView(u8"P"));
    auto body = core::MakeRef<Label>(core::DefaultAllocator(), StringView(u8"a"));
    panel->SetContent(RefPtr<View>(body.Get()));
    panel->SetContent({}); // clear
    panel->SetTitle(u8"New Title");
    panel->SetPreferredContentSize(320.0f, 240.0f);
    CHECK_FALSE(panel->IsCollapsed());
}

TEST_CASE("FloatingPanel: OnClose notifies subscribers")
{
    auto panel = core::MakeRef<FloatingPanel>(core::DefaultAllocator(), StringView(u8"P"));
    int closed = 0;
    panel->OnClose.Add([&closed]() { ++closed; });
    panel->OnClose.Invoke();
    CHECK(closed == 1);
}
