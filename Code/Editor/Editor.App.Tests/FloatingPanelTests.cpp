// FloatingPanel control tests (editor.app:floating_panel): the reusable draggable / resizable /
// collapsible / closable float panel. Exercised headlessly - no UIContext, no layout pass - so this
// covers the "safe without a parent" contract, the collapse toggle, content swap, and the OnClose
// notification. Drag / resize / clamp geometry needs a laid-out parent + a captured mouse (the
// in-editor path) and is out of scope for a unit test.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import editor.app;

using namespace foundation::core;
using namespace editor;

namespace ui = foundation::ui;

TEST_CASE("FloatingPanel: constructs and defaults to expanded")
{
    auto panel = MakeRef<FloatingPanel>(DefaultAllocator(), StringView(u8"Brush"));
    CHECK(panel.Get() != nullptr);
    CHECK_FALSE(panel->IsCollapsed());
}

TEST_CASE("FloatingPanel: collapse toggles and is idempotent")
{
    auto panel = MakeRef<FloatingPanel>(DefaultAllocator(), StringView(u8"Tools"));
    panel->SetCollapsed(true);
    CHECK(panel->IsCollapsed());
    panel->SetCollapsed(true); // repeated set is a no-op
    CHECK(panel->IsCollapsed());
    panel->SetCollapsed(false);
    CHECK_FALSE(panel->IsCollapsed());
}

TEST_CASE("FloatingPanel: content swap and drag/resize are safe without a parent")
{
    auto panel = MakeRef<FloatingPanel>(DefaultAllocator(), StringView(u8"P"));
    auto body = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"body"));
    panel->SetContent(RefPtr<ui::View>(body.Get()));
    panel->SetContent({}); // clear
    // No parent -> ClampToParent early-returns; no LayoutParams -> ApplySize early-returns.
    panel->DragBy(10.0f, 5.0f);
    panel->ResizeBy(20.0f, 20.0f);
    CHECK_FALSE(panel->IsCollapsed());
}

TEST_CASE("FloatingPanel: OnClose notifies subscribers")
{
    auto panel = MakeRef<FloatingPanel>(DefaultAllocator(), StringView(u8"P"));
    int closed = 0;
    panel->OnClose.Add([&closed]() { ++closed; });
    panel->OnClose.Invoke();
    CHECK(closed == 1);
}
