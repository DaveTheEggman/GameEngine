// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Ported from Sedulous.UI.Tests/src/ViewGroupTests.bf (faithful; RefPtr views, `===` -> pointer ==,
// Vector2 -> Float2, LayoutParams -> RefPtr<LayoutParams>).
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
static core::RefPtr<TestView> MakeTestView(f32 w = 50, f32 h = 30)
{
    return core::MakeRef<TestView>(core::DefaultAllocator(), w, h);
}
static core::RefPtr<TestGroup> MakeTestGroup()
{
    return core::MakeRef<TestGroup>(core::DefaultAllocator());
}

TEST_CASE("viewgroup: AddView_IncreasesChildCount")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestGroup> group = MakeTestGroup();
    root->AddView(group.Get());

    CHECK(group->ChildCount() == 0u);
    core::RefPtr<TestView> child = MakeTestView();
    group->AddView(child.Get());
    CHECK(group->ChildCount() == 1u);
    CHECK(group->GetChildAt(0) == child.Get());
}

TEST_CASE("viewgroup: AddView_SetsParentAndContext")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestGroup> group = MakeTestGroup();
    root->AddView(group.Get());

    core::RefPtr<TestView> child = MakeTestView();
    group->AddView(child.Get());
    CHECK(child->Parent == group.Get());
    CHECK(child->Context == &ctx);
}

TEST_CASE("viewgroup: AddView_RejectsNull")
{
    core::RefPtr<TestGroup> group = MakeTestGroup();
    group->AddView(nullptr);
    CHECK(group->ChildCount() == 0u);
}

TEST_CASE("viewgroup: AddView_RejectsSelf")
{
    core::RefPtr<TestGroup> group = MakeTestGroup();
    group->AddView(group.Get());
    CHECK(group->ChildCount() == 0u);
}

TEST_CASE("viewgroup: AddView_RejectsDuplicate")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestGroup> group = MakeTestGroup();
    root->AddView(group.Get());

    core::RefPtr<TestView> child = MakeTestView();
    group->AddView(child.Get());
    group->AddView(child.Get()); // duplicate
    CHECK(group->ChildCount() == 1u);
}

TEST_CASE("viewgroup: AddView_ReparentsFromOldParent")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestGroup> groupA = MakeTestGroup();
    core::RefPtr<TestGroup> groupB = MakeTestGroup();
    root->AddView(groupA.Get());
    root->AddView(groupB.Get());

    core::RefPtr<TestView> child = MakeTestView();
    groupA->AddView(child.Get());
    CHECK(groupA->ChildCount() == 1u);

    groupB->AddView(child.Get());
    CHECK(groupA->ChildCount() == 0u);
    CHECK(groupB->ChildCount() == 1u);
    CHECK(child->Parent == groupB.Get());
}

TEST_CASE("viewgroup: AddView_CreatesDefaultLayoutParams")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestGroup> group = MakeTestGroup();
    root->AddView(group.Get());

    core::RefPtr<TestView> child = MakeTestView();
    CHECK(!child->LayoutParams);
    group->AddView(child.Get());
    CHECK(child->LayoutParams);
}

TEST_CASE("viewgroup: AddView_ReplacesOldLayoutParams")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestGroup> group = MakeTestGroup();
    root->AddView(group.Get());

    core::RefPtr<TestView> child = MakeTestView();
    core::RefPtr<LayoutParams> oldLp = core::MakeRef<LayoutParams>(core::DefaultAllocator());
    child->LayoutParams = oldLp;

    core::RefPtr<LayoutParams> newLp = core::MakeRef<LayoutParams>(core::DefaultAllocator());
    group->AddView(child.Get(), newLp);
    CHECK(child->LayoutParams.Get() == newLp.Get());
}

TEST_CASE("viewgroup: RemoveView_ClearsParentAndContext")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestGroup> group = MakeTestGroup();
    root->AddView(group.Get());

    core::RefPtr<TestView> child = MakeTestView();
    group->AddView(child.Get());
    group->RemoveView(child.Get());

    CHECK(child->Parent == nullptr);
    CHECK(child->Context == nullptr);
    CHECK(group->ChildCount() == 0u);
}

TEST_CASE("viewgroup: RemoveView_WithDelete")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestGroup> group = MakeTestGroup();
    root->AddView(group.Get());

    core::RefPtr<TestView> child = MakeTestView();
    group->AddView(child.Get());
    group->RemoveView(child.Get(), true);
    CHECK(group->ChildCount() == 0u);
}

TEST_CASE("viewgroup: RemoveAllViews_ClearsAll")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestGroup> group = MakeTestGroup();
    root->AddView(group.Get());

    group->AddView(MakeTestView().Get());
    group->AddView(MakeTestView().Get());
    group->AddView(MakeTestView().Get());
    CHECK(group->ChildCount() == 3u);

    group->RemoveAllViews(true);
    CHECK(group->ChildCount() == 0u);
}

TEST_CASE("viewgroup: InsertView_AtIndex")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestGroup> group = MakeTestGroup();
    root->AddView(group.Get());

    core::RefPtr<TestView> a = MakeTestView();
    core::RefPtr<TestView> b = MakeTestView();
    core::RefPtr<TestView> c = MakeTestView();
    group->AddView(a.Get());
    group->AddView(c.Get());
    group->InsertView(b.Get(), 1);

    CHECK(group->ChildCount() == 3u);
    CHECK(group->GetChildAt(0) == a.Get());
    CHECK(group->GetChildAt(1) == b.Get());
    CHECK(group->GetChildAt(2) == c.Get());
}

TEST_CASE("viewgroup: ContentBounds_AccountsForPadding")
{
    core::RefPtr<TestGroup> group = MakeTestGroup();
    group->Padding = Thickness{10, 5, 10, 5};
    group->Layout(0, 0, 200, 100);

    const Rectangle cb = group->ContentBounds();
    CHECK(cb.x == 10);
    CHECK(cb.y == 5);
    CHECK(cb.width == doctest::Approx(180));
    CHECK(cb.height == doctest::Approx(90));
}

TEST_CASE("viewgroup: HitTest_ReturnsDeepestChild")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get(), 400, 300);

    core::RefPtr<TestGroup> group = MakeTestGroup();
    root->AddView(group.Get());
    core::RefPtr<TestView> child = MakeTestView(400, 300);
    group->AddView(child.Get());

    LayoutPass(ctx, root.Get());

    View* hit = root->HitTest(Float2{10, 10});
    CHECK(hit == child.Get());
}

TEST_CASE("viewgroup: HitTest_ReturnsNullOutsideBounds")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get(), 400, 300);

    LayoutPass(ctx, root.Get());

    View* hit = root->HitTest(Float2{500, 500});
    CHECK(hit == nullptr);
}

TEST_CASE("viewgroup: HitTest_SkipsNotVisible")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get(), 400, 300);

    core::RefPtr<TestView> child = MakeTestView(400, 300);
    child->Visibility = Visibility::Hidden;
    root->AddView(child.Get());

    LayoutPass(ctx, root.Get());

    View* hit = root->HitTest(Float2{10, 10});
    CHECK(hit != child.Get());
}

TEST_CASE("viewgroup: HitTest_SkipsNotInteractionEnabled")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get(), 400, 300);

    core::RefPtr<TestView> child = MakeTestView(400, 300);
    child->IsInteractionEnabled = false;
    root->AddView(child.Get());

    LayoutPass(ctx, root.Get());

    View* hit = root->HitTest(Float2{10, 10});
    CHECK(hit != child.Get());
}

TEST_CASE("viewgroup: HitTest_PassThroughNonHitTestVisible")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get(), 400, 300);

    core::RefPtr<TestGroup> group = MakeTestGroup();
    group->IsHitTestVisible = false;
    root->AddView(group.Get());

    core::RefPtr<TestView> child = MakeTestView(400, 300);
    group->AddView(child.Get());

    LayoutPass(ctx, root.Get());

    View* hit = root->HitTest(Float2{10, 10});
    CHECK(hit == child.Get());
}

TEST_CASE("viewgroup: HitTest_ReverseOrder_TopmostFirst")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get(), 400, 300);

    core::RefPtr<TestView> a = MakeTestView(400, 300);
    core::RefPtr<TestView> b = MakeTestView(400, 300);
    root->AddView(a.Get());
    root->AddView(b.Get()); // b on top

    LayoutPass(ctx, root.Get());

    View* hit = root->HitTest(Float2{10, 10});
    CHECK(hit == b.Get());
}

// === FindByName ===

TEST_CASE("viewgroup: FindByName_DirectChild")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestView> child = MakeTestView();
    child->Name = String(u8"target");
    root->AddView(child.Get());

    CHECK(root->FindByName(u8"target") == child.Get());
}

TEST_CASE("viewgroup: FindByName_NestedChild")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestGroup> group = MakeTestGroup();
    root->AddView(group.Get());
    core::RefPtr<TestView> nested = MakeTestView();
    nested->Name = String(u8"deep");
    group->AddView(nested.Get());

    CHECK(root->FindByName(u8"deep") == nested.Get());
}

TEST_CASE("viewgroup: FindByName_NotFound")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    root->AddView(MakeTestView().Get());
    CHECK(root->FindByName(u8"nonexistent") == nullptr);
}

TEST_CASE("viewgroup: FindByName_Typed")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestView> child = MakeTestView();
    child->Name = String(u8"typed");
    root->AddView(child.Get());

    CHECK(root->FindByName<TestView>(u8"typed") == child.Get());
    CHECK(root->FindByName<TestGroup>(u8"typed") == nullptr);
}

TEST_CASE("viewgroup: FindByName_DeeplyNested")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestGroup> level1 = MakeTestGroup();
    core::RefPtr<TestGroup> level2 = MakeTestGroup();
    core::RefPtr<TestView> target = MakeTestView();
    target->Name = String(u8"deep-target");
    root->AddView(level1.Get());
    level1->AddView(level2.Get());
    level2->AddView(target.Get());

    CHECK(root->FindByName(u8"deep-target") == target.Get());
}

// A transformed child hit-tests at its DRAWN position, not its layout bounds (ViewTransform inverse in
// HitTest). The child fills the group; a +50px x-translation shifts its drawn position right by 50.
TEST_CASE("viewgroup: HitTest_AppliesInverseTransform")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get(), 400, 300);

    core::RefPtr<TestGroup> group = MakeTestGroup();
    root->AddView(group.Get());
    core::RefPtr<TestView> child = MakeTestView(400, 300);
    group->AddView(child.Get());
    LayoutPass(ctx, root.Get());

    child->Transform.Translation =
        Float2{50, 0}; // drawn 50px to the right (transform doesn't relayout)

    // At the drawn position the child is hit...
    CHECK(root->HitTest(Float2{60, 10}) == child.Get());
    // ...but at its old layout position (now empty because the child drew away) it is not.
    CHECK(root->HitTest(Float2{10, 10}) != child.Get());
}

// MoveView: a pure reorder - the child changes index without a Detach/Attach round-trip
// (Context/registration survive), clamped, and a non-child is ignored.
TEST_CASE("viewgroup: MoveView_ReordersWithoutDetach")
{
    UIContext ctx;
    core::RefPtr<RootView> root = MakeRoot();
    Init(ctx, root.Get());

    core::RefPtr<TestGroup> group = MakeTestGroup();
    root->AddView(group.Get());
    core::RefPtr<TestView> a = MakeTestView();
    core::RefPtr<TestView> b = MakeTestView();
    core::RefPtr<TestView> c = MakeTestView();
    group->AddView(a.Get());
    group->AddView(b.Get());
    group->AddView(c.Get());

    group->MoveView(c.Get(), 0); // [c, a, b]
    CHECK(group->GetChildAt(0) == c.Get());
    CHECK(group->GetChildAt(1) == a.Get());
    CHECK(group->GetChildAt(2) == b.Get());
    CHECK(c->Parent == group.Get());
    CHECK(c->Context == &ctx); // no detach happened

    group->MoveView(c.Get(), 99); // clamped to last: [a, b, c]
    CHECK(group->GetChildAt(2) == c.Get());
    CHECK(group->GetChildAt(0) == a.Get());

    group->MoveView(a.Get(), 1); // forward move: [b, a, c]
    CHECK(group->GetChildAt(0) == b.Get());
    CHECK(group->GetChildAt(1) == a.Get());

    // A view that is not a child is ignored.
    core::RefPtr<TestView> stranger = MakeTestView();
    group->MoveView(stranger.Get(), 0);
    CHECK(group->ChildCount() == 3u);
    CHECK(group->GetChildAt(0) == b.Get());
}
