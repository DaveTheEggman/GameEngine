// Draconic GUI - Menu tests: opening at a position as a popup, activating an item (runs the
// action + closes), and dismissal (outside click / Escape) via the dispatcher popup support.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.gui;

using namespace draconic::gui;
namespace core = draconic::core;

namespace
{
    template <typename T> core::RefPtr<T> Make() { return core::MakeRef<T>(core::DefaultAllocator()); }
}

TEST_CASE("menu: sizes to its items")
{
    auto menu = Make<Menu>();
    menu->SetWidth(140.0f);
    menu->SetItemHeight(25.0f);
    menu->AddItem(core::StringView(u8"Cut"), [] {});
    menu->AddItem(core::StringView(u8"Copy"), [] {});
    menu->AddItem(core::StringView(u8"Paste"), [] {});

    CHECK(menu->ItemCount() == 3);
    CHECK(menu->GetSize().x == doctest::Approx(140.0f));
    CHECK(menu->GetSize().y == doctest::Approx(75.0f)); // 3 * 25
}

TEST_CASE("menu: Open shows it as the dispatcher popup at the given position")
{
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 400.0f, 400.0f });
    auto anchor = Make<UIWidget>();
    anchor->SetSize(core::Float2{ 10.0f, 10.0f });
    root->AddChild(anchor.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    auto menu = Make<Menu>();
    menu->SetItemHeight(26.0f);
    menu->AddItem(core::StringView(u8"One"), [] {});
    menu->AddItem(core::StringView(u8"Two"), [] {});

    menu->Open(*anchor, core::Float2{ 100.0f, 80.0f });
    CHECK(menu->IsOpen());
    CHECK(d->GetPopup() == menu.Get());
    CHECK(menu->GetPosition().x == doctest::Approx(100.0f));
    CHECK(menu->GetPosition().y == doctest::Approx(80.0f));
    CHECK(menu->GetParent() == root.Get()); // attached top-level
}

TEST_CASE("menu: activating an item runs its action and closes")
{
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 400.0f, 400.0f });
    auto anchor = Make<UIWidget>();
    root->AddChild(anchor.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    int cut = 0, paste = 0;
    auto menu = Make<Menu>();
    menu->SetItemHeight(26.0f);
    menu->AddItem(core::StringView(u8"Cut"), [&] { ++cut; });
    menu->AddItem(core::StringView(u8"Paste"), [&] { ++paste; });
    menu->Open(*anchor, core::Float2{ 50.0f, 50.0f });

    // Item 1 (Paste) spans y in [50 + 26, 50 + 52) = [76, 102); click it.
    d->InjectMouseDown(core::Float2{ 60.0f, 88.0f }, MouseButton::Left);
    d->InjectMouseUp(core::Float2{ 60.0f, 88.0f }, MouseButton::Left);
    CHECK(paste == 1);
    CHECK(cut == 0);
    CHECK_FALSE(menu->IsOpen());
    CHECK(d->GetPopup() == nullptr);
    CHECK(menu->GetParent() == nullptr); // removed from the tree
}

TEST_CASE("menu: outside click and Escape dismiss it")
{
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 400.0f, 400.0f });
    auto anchor = Make<UIWidget>();
    root->AddChild(anchor.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    auto menu = Make<Menu>();
    menu->AddItem(core::StringView(u8"One"), [] {});
    menu->Open(*anchor, core::Float2{ 50.0f, 50.0f });
    REQUIRE(menu->IsOpen());

    d->InjectMouseDown(core::Float2{ 300.0f, 300.0f }, MouseButton::Left); // outside
    CHECK_FALSE(menu->IsOpen());

    // Reopen and dismiss with Escape.
    menu->Open(*anchor, core::Float2{ 50.0f, 50.0f });
    REQUIRE(menu->IsOpen());
    d->InjectKeyDown(static_cast<core::u32>(KeyCode::Escape));
    CHECK_FALSE(menu->IsOpen());
}
