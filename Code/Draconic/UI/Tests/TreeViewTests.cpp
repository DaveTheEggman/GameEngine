// Ported from Sedulous.UI.Tests/src/TreeViewTests.bf (faithful). SimpleTreeAdapter test double from
// TestHelpers.h; the borrowed adapter is declared before the TreeView so it outlives it. Beef property
// passthroughs -> methods (tv->FlatAdapter()/Selection()); HierarchicalState.CaptureState/ApplyState take
// a TreeView&. Logic only, no font.
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
static core::RefPtr<TreeView> MakeTree() { return core::MakeRef<TreeView>(core::DefaultAllocator()); }

TEST_CASE("tree-view: SetAdapter_ShowsRootItems")
{
    UIContext ctx; auto root = MakeRoot(); Init(ctx, root.Get(), 200, 300);
    SimpleTreeAdapter adapter;
    auto tv = MakeTree();
    tv->SetAdapter(&adapter);
    root->AddView(tv.Get());
    LayoutPass(ctx, root.Get());

    CHECK(tv->FlatAdapter() != nullptr);
    CHECK(tv->FlatAdapter()->ItemCount() == 3); // 3 roots
}

TEST_CASE("tree-view: ToggleExpand_ChangesCount")
{
    SimpleTreeAdapter adapter;
    auto tv = MakeTree();
    tv->SetAdapter(&adapter);

    CHECK(tv->FlatAdapter()->ItemCount() == 3);
    tv->ToggleExpand(0); // expand root 0
    CHECK(tv->FlatAdapter()->ItemCount() == 5); // 3 + 2 children
    tv->ToggleExpand(0); // collapse
    CHECK(tv->FlatAdapter()->ItemCount() == 3);
}

TEST_CASE("tree-view: ExpandNode_AddsChildren")
{
    SimpleTreeAdapter adapter;
    auto tv = MakeTree();
    tv->SetAdapter(&adapter);

    tv->FlatAdapter()->Expand(1); // root 1 has 1 child
    CHECK(tv->FlatAdapter()->ItemCount() == 4);
    CHECK(tv->FlatAdapter()->GetNodeId(2) == 20); // child of root 1
}

TEST_CASE("tree-view: CollapseNode_RemovesChildren")
{
    SimpleTreeAdapter adapter;
    auto tv = MakeTree();
    tv->SetAdapter(&adapter);

    tv->FlatAdapter()->Expand(0);
    tv->FlatAdapter()->Expand(1);
    CHECK(tv->FlatAdapter()->ItemCount() == 6);

    tv->FlatAdapter()->Collapse(0);
    CHECK(tv->FlatAdapter()->ItemCount() == 4);
}

TEST_CASE("tree-view: Selection")
{
    SimpleTreeAdapter adapter;
    auto tv = MakeTree();
    tv->SetAdapter(&adapter);

    tv->Selection().Select(0);
    CHECK(tv->Selection().IsSelected(0));
    CHECK(tv->Selection().FirstSelected() == 0);
}

TEST_CASE("tree-view: HierarchicalState_CaptureRestore")
{
    SimpleTreeAdapter adapter;
    auto tv = MakeTree();
    tv->SetAdapter(&adapter);

    tv->FlatAdapter()->Expand(0);
    tv->FlatAdapter()->Expand(1);
    tv->Selection().Select(2);

    HierarchicalState state;
    state.CaptureState(*tv);

    tv->FlatAdapter()->Collapse(0);
    tv->FlatAdapter()->Collapse(1);
    tv->Selection().ClearSelection();
    CHECK(tv->FlatAdapter()->ItemCount() == 3);

    state.ApplyState(*tv);
    CHECK(tv->FlatAdapter()->IsExpanded(0));
    CHECK(tv->FlatAdapter()->IsExpanded(1));
    CHECK(tv->FlatAdapter()->ItemCount() == 6);
    CHECK(tv->Selection().IsSelected(2));
}
