// Ported from Sedulous.UI.Tests/src/DataTests.bf - the ViewRecycler + SelectionModel subset (the tree
// adapter cases land with TreeView). Beef `SimpleListAdapter : ListAdapterBase` with CreateView returning
// a raw owned View -> CreateView returns RefPtr<View>; recycle/acquire move refs (RAII, no `delete`);
// `===` ref-equality -> pointer ==.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
#include "TestHelpers.h"

using namespace draconic::ui;
using namespace draconic::ui::tests;
using namespace draconic::core;
namespace core = draconic::core;

namespace
{
    class SimpleListAdapter : public ListAdapterBase
    {
    public:
        explicit SimpleListAdapter(i32 count) : m_count(count) {}
        [[nodiscard]] i32 ItemCount() const override { return m_count; }
        [[nodiscard]] core::RefPtr<View> CreateView(i32) override { return core::MakeRef<TestView>(core::DefaultAllocator(), 100.0f, 30.0f); }
        void BindView(View*, i32) override {}
    private:
        i32 m_count;
    };
}

// === ViewRecycler ===

TEST_CASE("data: ViewRecycler_AcquireReturnsNull_WhenEmpty")
{
    ViewRecycler recycler;
    CHECK(!recycler.Acquire(0));
}

TEST_CASE("data: ViewRecycler_RecycleAndAcquire_ReusesView")
{
    ViewRecycler recycler;
    auto view = core::MakeRef<TestView>(core::DefaultAllocator());
    View* raw = view.Get();
    recycler.Recycle(Move(view), 0);
    auto reused = recycler.Acquire(0);
    CHECK(reused.Get() == raw);
    CHECK(recycler.ReusedCount() == 1);
    CHECK(recycler.RecycledCount() == 1);
}

TEST_CASE("data: ViewRecycler_GetOrCreate_CreatesWhenEmpty")
{
    ViewRecycler recycler;
    SimpleListAdapter adapter(5);
    auto view = recycler.GetOrCreate(adapter, 0);
    CHECK(view);
    CHECK(recycler.CreatedCount() == 1);
}

TEST_CASE("data: ViewRecycler_DiagnosticCounters")
{
    ViewRecycler recycler;
    SimpleListAdapter adapter(5);
    auto v1 = recycler.GetOrCreate(adapter, 0);
    CHECK(recycler.CreatedCount() == 1);
    View* raw1 = v1.Get();
    recycler.Recycle(Move(v1), 0);
    CHECK(recycler.RecycledCount() == 1);
    auto v2 = recycler.GetOrCreate(adapter, 1);
    CHECK(recycler.ReusedCount() == 1);
    CHECK(v2.Get() == raw1);
}

// === SelectionModel ===

TEST_CASE("data: SelectionModel_SingleMode_ReplacesSelection")
{
    SelectionModel sel; sel.Mode = SelectionMode::Single;
    sel.Select(0);
    sel.Select(1);
    CHECK(!sel.IsSelected(0));
    CHECK(sel.IsSelected(1));
    CHECK(sel.SelectedCount() == 1u);
}

TEST_CASE("data: SelectionModel_MultipleMode_Accumulates")
{
    SelectionModel sel; sel.Mode = SelectionMode::Multiple;
    sel.Select(0); sel.Select(1); sel.Select(2);
    CHECK(sel.IsSelected(0));
    CHECK(sel.IsSelected(1));
    CHECK(sel.IsSelected(2));
    CHECK(sel.SelectedCount() == 3u);
}

TEST_CASE("data: SelectionModel_Toggle")
{
    SelectionModel sel; sel.Mode = SelectionMode::Multiple;
    sel.Select(0);
    sel.Toggle(0); CHECK(!sel.IsSelected(0));
    sel.Toggle(0); CHECK(sel.IsSelected(0));
}

TEST_CASE("data: SelectionModel_SelectRange")
{
    SelectionModel sel; sel.Mode = SelectionMode::Multiple;
    sel.SelectRange(2, 5);
    CHECK(sel.SelectedCount() == 4u);
    CHECK(sel.IsSelected(2));
    CHECK(sel.IsSelected(3));
    CHECK(sel.IsSelected(4));
    CHECK(sel.IsSelected(5));
}

TEST_CASE("data: SelectionModel_ClearSelection")
{
    SelectionModel sel; sel.Mode = SelectionMode::Multiple;
    sel.Select(0); sel.Select(1);
    sel.ClearSelection();
    CHECK(sel.SelectedCount() == 0u);
}

TEST_CASE("data: SelectionModel_ShiftIndices_Insert")
{
    SelectionModel sel; sel.Mode = SelectionMode::Multiple;
    sel.Select(2); sel.Select(4);
    sel.ShiftIndices(3, 1); // insert at 3
    CHECK(sel.IsSelected(2)); // unchanged
    CHECK(sel.IsSelected(5)); // shifted from 4
}

TEST_CASE("data: SelectionModel_NoneMode_Ignores")
{
    SelectionModel sel; sel.Mode = SelectionMode::None;
    sel.Select(0);
    CHECK(sel.SelectedCount() == 0u);
}
