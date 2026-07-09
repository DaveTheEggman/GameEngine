// Ported from Sedulous.UI.Tests/src/TabViewTests.bf (faithful). Beef SelectedIndex/TabCount get/set ->
// methods; `new TestView()` content -> a MakeRef<TestView> whose ref AddView adopts (kept as a local when
// the test inspects its Visibility). KeyEventArgs.Set + OnKeyDown drive the keyboard case. No font needed.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
#include "TestHelpers.h"

using namespace draconic::ui;
using namespace draconic::ui::tests;
using namespace draconic::core;
namespace core = draconic::core;

static core::RefPtr<RootView> MakeRoot() { return core::MakeRef<RootView>(core::DefaultAllocator()); }
static core::RefPtr<TabView> MakeTabs() { return core::MakeRef<TabView>(core::DefaultAllocator()); }
static core::RefPtr<TestView> MakeView(f32 w = 50.0f, f32 h = 30.0f) { return core::MakeRef<TestView>(core::DefaultAllocator(), w, h); }

TEST_CASE("tab-view: AddTab_SelectsFirst")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto tabs = MakeTabs();
    tabs->AddTab(u8"Tab 1", MakeView(100, 100).Get());
    root->AddView(tabs.Get());
    LayoutPass(ctx, root.Get());

    CHECK(tabs->SelectedIndex() == 0);
    CHECK(tabs->TabCount() == 1u);
}

TEST_CASE("tab-view: SwitchTab")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto tabs = MakeTabs();
    auto content1 = MakeView(100, 100);
    auto content2 = MakeView(100, 100);
    tabs->AddTab(u8"Tab 1", content1.Get());
    tabs->AddTab(u8"Tab 2", content2.Get());
    root->AddView(tabs.Get());

    CHECK(tabs->SelectedIndex() == 0);
    CHECK(content1->Visibility == Visibility::Visible);
    CHECK(content2->Visibility == Visibility::Gone);

    tabs->SetSelectedIndex(1);
    CHECK(content1->Visibility == Visibility::Gone);
    CHECK(content2->Visibility == Visibility::Visible);
}

TEST_CASE("tab-view: TabChangedEvent")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto tabs = MakeTabs();
    tabs->AddTab(u8"A", MakeView().Get());
    tabs->AddTab(u8"B", MakeView().Get());
    root->AddView(tabs.Get());

    i32 lastIdx = -1;
    tabs->OnTabChanged.Add(Event<void(TabView*, i32)>::Handler{ [&lastIdx](TabView*, i32 idx) { lastIdx = idx; } });

    tabs->SetSelectedIndex(1);
    CHECK(lastIdx == 1);
}

TEST_CASE("tab-view: RemoveTab")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto tabs = MakeTabs();
    tabs->AddTab(u8"A", MakeView().Get());
    tabs->AddTab(u8"B", MakeView().Get());
    tabs->AddTab(u8"C", MakeView().Get());
    root->AddView(tabs.Get());

    tabs->SetSelectedIndex(2);
    tabs->RemoveTab(2);
    CHECK(tabs->TabCount() == 2u);
    CHECK(tabs->SelectedIndex() == 1); // clamps to last
}

TEST_CASE("tab-view: RemoveTab_AdjustsSelection")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto tabs = MakeTabs();
    tabs->AddTab(u8"A", MakeView().Get());
    tabs->AddTab(u8"B", MakeView().Get());
    root->AddView(tabs.Get());

    tabs->SetSelectedIndex(1);
    tabs->RemoveTab(1);
    CHECK(tabs->TabCount() == 1u);
    CHECK(tabs->SelectedIndex() == 0);
}

TEST_CASE("tab-view: KeyboardNavigation")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 400, 300);
    auto tabs = MakeTabs();
    tabs->AddTab(u8"A", MakeView().Get());
    tabs->AddTab(u8"B", MakeView().Get());
    tabs->AddTab(u8"C", MakeView().Get());
    root->AddView(tabs.Get());

    CHECK(tabs->SelectedIndex() == 0);

    KeyEventArgs right; right.Set(KeyCode::Right, KeyModifiers::None, false);
    tabs->OnKeyDown(right);
    CHECK(tabs->SelectedIndex() == 1);

    KeyEventArgs left; left.Set(KeyCode::Left, KeyModifiers::None, false);
    tabs->OnKeyDown(left);
    CHECK(tabs->SelectedIndex() == 0);

    tabs->OnKeyDown(left); // can't go below 0
    CHECK(tabs->SelectedIndex() == 0);
}
