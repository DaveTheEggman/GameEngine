// Ported from Sedulous.UI.Tests/src/ContextMenuTests.bf (faithful). Beef `delegate void()` -> Function<
// void()>; Beef nullable String -> empty String check; MenuItem.CreateSeparator returns UniquePtr; menu
// item/submenu structure only (no popup, no font).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;

using namespace draconic::ui;
using namespace draconic::core;
namespace core = draconic::core;

static core::RefPtr<ContextMenu> MakeMenu() { return core::MakeRef<ContextMenu>(core::DefaultAllocator()); }

TEST_CASE("context-menu: MenuItem_Properties")
{
    MenuItem item(u8"Test", []() {}, true);
    CHECK(!item.Label.IsEmpty());
    CHECK(item.Label == u8"Test");
    CHECK(item.Enabled == true);
    CHECK(!item.IsSeparator);
    CHECK(!item.Submenu);
}

TEST_CASE("context-menu: MenuItem_CreateSeparator")
{
    auto item = MenuItem::CreateSeparator();
    CHECK(item->IsSeparator);
    CHECK(item->Label.IsEmpty());
    CHECK(!item->Action);
}

TEST_CASE("context-menu: AddItem_IncreasesCount")
{
    auto menu = MakeMenu();
    CHECK(menu->ItemCount() == 0);

    menu->AddItem(u8"Item 1", []() {});
    CHECK(menu->ItemCount() == 1);

    menu->AddItem(u8"Item 2", []() {});
    CHECK(menu->ItemCount() == 2);
}

TEST_CASE("context-menu: AddSeparator_IncreasesCount")
{
    auto menu = MakeMenu();
    menu->AddItem(u8"Item 1", []() {});
    menu->AddSeparator();
    menu->AddItem(u8"Item 2", []() {});
    CHECK(menu->ItemCount() == 3);
}

TEST_CASE("context-menu: AddSubmenu_CreatesItemWithSubmenu")
{
    auto menu = MakeMenu();
    MenuItem* sub = menu->AddSubmenu(u8"More");
    CHECK(sub != nullptr);
    CHECK(sub->Submenu);
    CHECK(!sub->Label.IsEmpty());
    CHECK(sub->Label == u8"More");
    CHECK(menu->ItemCount() == 1);
}

TEST_CASE("context-menu: IsFocusable")
{
    auto menu = MakeMenu();
    CHECK(menu->IsFocusable);
}
