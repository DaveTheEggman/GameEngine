// Ported from Sedulous.UI.Tests/src/ListViewTests.bf (faithful). Beef get/set props -> methods
// (lv->SetAdapter / lv->ScrollY()); SimpleListAdapter test double lives in TestHelpers.h. The adapter is
// borrowed (pattern-B) - declared before the ListView so it outlives it. Logic only, no font.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import foundation.core;
import foundation.ui;
#include "TestHelpers.h"

using namespace foundation::ui;
using namespace foundation::ui::tests;
using namespace foundation::core;
namespace core = foundation::core;

static core::RefPtr<RootView> MakeRoot()
{
    return core::MakeRef<RootView>(core::DefaultAllocator());
}
static core::RefPtr<ListView> MakeList()
{
    return core::MakeRef<ListView>(core::DefaultAllocator());
}

TEST_CASE("list-view: NoAdapter_NoViews")
{
    UIContext ctx;
    auto root = MakeRoot();
    Init(ctx, root.Get(), 200, 300);
    auto lv = MakeList();
    root->AddView(lv.Get());
    LayoutPass(ctx, root.Get());

    // VisualChildCount = active views (0) + scrollbar (1).
    CHECK(lv->VisualChildCount() == 1u);
}

TEST_CASE("list-view: IsFocusable")
{
    auto lv = MakeList();
    CHECK(lv->IsFocusable);
    CHECK(lv->IsTabStop);
}

TEST_CASE("list-view: SetAdapter_CreatesVisibleViews")
{
    UIContext ctx;
    auto root = MakeRoot();
    Init(ctx, root.Get(), 200, 300);
    SimpleListAdapter adapter(100);
    auto lv = MakeList();
    lv->ItemHeight.SetValue(30);
    lv->SetAdapter(&adapter);
    root->AddView(lv.Get());
    LayoutPass(ctx, root.Get());

    // ~10 visible items (300 / 30) + scrollbar.
    CHECK(lv->VisualChildCount() > 1u);
}

TEST_CASE("list-view: ScrollBy_ClampsBounds")
{
    UIContext ctx;
    auto root = MakeRoot();
    Init(ctx, root.Get(), 200, 300);
    SimpleListAdapter adapter(100);
    auto lv = MakeList();
    lv->ItemHeight.SetValue(30);
    lv->SetAdapter(&adapter);
    root->AddView(lv.Get());
    LayoutPass(ctx, root.Get());

    lv->ScrollBy(-1000);
    CHECK(lv->ScrollY() == 0);

    lv->ScrollBy(999999);
    CHECK(lv->ScrollY() == lv->MaxScrollY());
}

TEST_CASE("list-view: GetItemAtY")
{
    UIContext ctx;
    auto root = MakeRoot();
    Init(ctx, root.Get(), 200, 300);
    SimpleListAdapter adapter(100);
    auto lv = MakeList();
    lv->ItemHeight.SetValue(30);
    lv->SetAdapter(&adapter);
    root->AddView(lv.Get());
    LayoutPass(ctx, root.Get());

    CHECK(lv->GetItemAtY(0) == 0);
    CHECK(lv->GetItemAtY(31) == 1);
    CHECK(lv->GetItemAtY(60) == 2);
}

TEST_CASE("list-view: Selection_SingleMode")
{
    SimpleListAdapter adapter(10);
    auto lv = MakeList();
    lv->SetAdapter(&adapter);

    lv->Selection.Select(3);
    CHECK(lv->Selection.IsSelected(3));
    CHECK(lv->Selection.SelectedCount() == 1u);

    lv->Selection.Select(5);
    CHECK(!lv->Selection.IsSelected(3));
    CHECK(lv->Selection.IsSelected(5));
}

TEST_CASE("list-view: NotifyDataChanged_PrunesOutOfRangeSelection")
{
    SimpleListAdapter big(10);
    SimpleListAdapter small(4);
    auto lv = MakeList();
    lv->SetAdapter(&big);
    lv->Selection.Select(2);
    lv->Selection.Toggle(8); // Single mode replaces; use two steps to end selected on 8
    CHECK(lv->Selection.IsSelected(8));

    lv->SetAdapter(&small); // data set shrank under the selection
    lv->NotifyDataChanged();

    // Index 8 no longer exists - it must be dropped, never left to silently highlight
    // whichever row a future grow puts at position 8.
    CHECK_FALSE(lv->Selection.IsSelected(8));
    CHECK(lv->Selection.SelectedCount() == 0u);
}

TEST_CASE("list-view: AdapterObserver_OnDataSetChanged")
{
    UIContext ctx;
    auto root = MakeRoot();
    Init(ctx, root.Get(), 200, 300);
    SimpleListAdapter adapter(10);
    auto lv = MakeList();
    lv->SetAdapter(&adapter);
    root->AddView(lv.Get());
    LayoutPass(ctx, root.Get());

    adapter.Count = 20;
    adapter.NotifyDataSetChanged();

    CHECK(lv->GetAdapter()->ItemCount() == 20);
}
