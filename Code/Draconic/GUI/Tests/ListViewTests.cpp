// Draconic GUI - ListView tests: virtualization (only visible rows realized), single selection
// via mouse + keyboard, wheel/scroll-into-view, and reacting to model updates.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.gui;

using namespace draconic::gui;
namespace core = draconic::core;

namespace
{
    template <typename T> core::RefPtr<T> Make() { return core::MakeRef<T>(core::DefaultAllocator()); }
    core::StringView SV(const char8_t* s) { return core::StringView(s); }

    core::Array<core::String> MakeItems(int count)
    {
        core::Array<core::String> items;
        for (int i = 0; i < count; ++i) items.PushBack(core::String(SV(u8"item")));
        return items;
    }
}

TEST_CASE("listview: virtualizes - only visible rows are realized")
{
    StringListModel model(MakeItems(1000));
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 300.0f, 300.0f });
    auto list = Make<ListView>();
    list->SetSize(core::Float2{ 200.0f, 100.0f });
    list->SetRowHeight(20.0f);
    root->AddChild(list.Get());
    list->SetModel(&model);

    // 100px viewport / 20px rows -> ~5 rows + 2 buffer = 7 realized, NOT 1000.
    CHECK(list->VisibleRowCount() == 7);
    CHECK(list->GetModel() == &model);
}

TEST_CASE("listview: clicking a row selects it and fires the callback")
{
    StringListModel model(MakeItems(10));
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 300.0f, 300.0f });
    auto list = Make<ListView>();
    list->SetSize(core::Float2{ 200.0f, 100.0f });
    list->SetRowHeight(20.0f);
    root->AddChild(list.Get());
    list->SetModel(&model);
    EventDispatcher* d = root->GetEventDispatcher();

    int selectedRow = -1;
    int calls = 0;
    list->SetOnSelectionChanged([&](ModelIndex i) { selectedRow = i.Row; ++calls; });

    // Row 2 spans y in [40, 60); click it.
    d->InjectMouseDown(core::Float2{ 50.0f, 50.0f }, MouseButton::Left);
    d->InjectMouseUp(core::Float2{ 50.0f, 50.0f }, MouseButton::Left);
    CHECK(list->GetSelectedRow() == 2);
    CHECK(selectedRow == 2);
    CHECK(calls == 1);
    CHECK(list->GetSelectedIndex() == MakeModelIndex(2));
}

TEST_CASE("listview: keyboard navigation moves the selection")
{
    StringListModel model(MakeItems(10));
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 300.0f, 300.0f });
    auto list = Make<ListView>();
    list->SetSize(core::Float2{ 200.0f, 100.0f });
    list->SetRowHeight(20.0f);
    root->AddChild(list.Get());
    list->SetModel(&model);
    EventDispatcher* d = root->GetEventDispatcher();
    list->RequestFocus();

    const auto key = [](KeyCode k) { return static_cast<core::u32>(k); };
    d->InjectKeyDown(key(KeyCode::Down)); // nothing selected -> row 0
    CHECK(list->GetSelectedRow() == 0);
    d->InjectKeyDown(key(KeyCode::Down));
    d->InjectKeyDown(key(KeyCode::Down));
    CHECK(list->GetSelectedRow() == 2);
    d->InjectKeyDown(key(KeyCode::Up));
    CHECK(list->GetSelectedRow() == 1);
    d->InjectKeyDown(key(KeyCode::End));
    CHECK(list->GetSelectedRow() == 9);
    d->InjectKeyDown(key(KeyCode::Home));
    CHECK(list->GetSelectedRow() == 0);
}

TEST_CASE("listview: selecting a far row scrolls it into view")
{
    StringListModel model(MakeItems(20)); // content 400, viewport 100 -> maxScroll 300
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 300.0f, 300.0f });
    auto list = Make<ListView>();
    list->SetSize(core::Float2{ 200.0f, 100.0f });
    list->SetRowHeight(20.0f);
    root->AddChild(list.Get());
    list->SetModel(&model);

    CHECK(list->ScrollOffset() == doctest::Approx(0.0f));
    list->SetSelectedRow(19); // last row -> bottom (400) - viewport (100) = 300
    CHECK(list->ScrollOffset() == doctest::Approx(300.0f));
    CHECK(list->GetSelectedRow() == 19);
}

TEST_CASE("listview: wheel scrolls and clamps")
{
    StringListModel model(MakeItems(20));
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 300.0f, 300.0f });
    auto list = Make<ListView>();
    list->SetSize(core::Float2{ 200.0f, 100.0f });
    list->SetRowHeight(20.0f);
    root->AddChild(list.Get());
    list->SetModel(&model);
    EventDispatcher* d = root->GetEventDispatcher();

    d->InjectMouseWheel(core::Float2{ 50.0f, 50.0f }, core::Float2{ 0.0f, -3.0f }); // down 3 rows
    CHECK(list->ScrollOffset() == doctest::Approx(60.0f));
    d->InjectMouseWheel(core::Float2{ 50.0f, 50.0f }, core::Float2{ 0.0f, -100.0f }); // clamps to maxScroll
    CHECK(list->ScrollOffset() == doctest::Approx(300.0f));
}

TEST_CASE("listview: reacts to model updates and clamps a stale selection")
{
    StringListModel model(MakeItems(5));
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 300.0f, 300.0f });
    auto list = Make<ListView>();
    list->SetSize(core::Float2{ 200.0f, 100.0f });
    list->SetRowHeight(20.0f);
    root->AddChild(list.Get());
    list->SetModel(&model);

    list->SetSelectedRow(4);
    CHECK(list->GetSelectedRow() == 4);

    model.SetItems(MakeItems(2)); // notifies the view -> selection 4 is now out of range
    CHECK(list->GetSelectedRow() == -1);

    model.AddItem(SV(u8"more"));
    CHECK(list->GetModel()->RowCount() == 3);
}
