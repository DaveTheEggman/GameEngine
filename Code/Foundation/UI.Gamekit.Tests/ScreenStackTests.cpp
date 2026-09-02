// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI.Gamekit - ScreenStack + UIScreen behavior (native, backend-neutral).
//
// Exercises the stack mechanics the `ui` script facade rides: push/pop/replace/clear + top/count, the
// Opaque "hide screens below" rule, the OnEnter/OnExit/OnShown/OnHidden lifecycle, focus save/restore
// across push/pop, and Back-does-not-empty-the-stack. Transitions default to None here, so structural
// changes run inline (the context is Idle in a test) - no frame pump needed.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import foundation.ui.gamekit;

using namespace foundation::core;
using namespace foundation::ui;
using namespace foundation::ui::gamekit;

namespace
{
    // A screen that counts its lifecycle callbacks + optionally carries a focusable button.
    class CountingScreen final : public UIScreen
    {
    public:
        int enter = 0, exit = 0, shown = 0, hidden = 0;
        RefPtr<Button> button;

        void OnEnter() override { ++enter; }
        void OnExit() override { ++exit; }
        void OnShown() override { ++shown; }
        void OnHidden() override { ++hidden; }

        void AddButton(StringView name)
        {
            button = MakeRef<Button>(DefaultAllocator(), StringView(u8"Go"));
            button->Name = String(name);
            AddView(button.Get());
        }
    };

    [[nodiscard]] RefPtr<CountingScreen> MakeScreen(ScreenMode mode = ScreenMode::Modal)
    {
        auto s = MakeRef<CountingScreen>(DefaultAllocator());
        s->SetMode(mode);
        return s;
    }

    struct Bed
    {
        UIContext context{DefaultAllocator()};
        RefPtr<RootView> root;
        ScreenStack stack;
        Bed()
        {
            root = MakeRef<RootView>(DefaultAllocator());
            context.AddRootView(root.Get());
            stack.Attach(root.Get());
        }
    };
}

TEST_CASE("screenstack: push/pop track top + count")
{
    Bed bed;
    CHECK(bed.stack.Count() == 0);
    CHECK(bed.stack.Top() == nullptr);

    auto a = MakeScreen();
    auto b = MakeScreen();
    CHECK(bed.stack.Push(a) == a.Get());
    CHECK(bed.stack.Count() == 1);
    CHECK(bed.stack.Top() == a.Get());

    bed.stack.Push(b);
    CHECK(bed.stack.Count() == 2);
    CHECK(bed.stack.Top() == b.Get());

    bed.stack.Pop();
    CHECK(bed.stack.Count() == 1);
    CHECK(bed.stack.Top() == a.Get());

    bed.stack.Pop();
    CHECK(bed.stack.Count() == 0);
    bed.stack.Pop(); // pop-when-empty is a safe no-op
    CHECK(bed.stack.Count() == 0);
}

TEST_CASE("screenstack: replace swaps the top in place")
{
    Bed bed;
    auto a = MakeScreen();
    auto b = MakeScreen();
    bed.stack.Push(a);
    CHECK(bed.stack.Replace(b) == b.Get());
    CHECK(bed.stack.Count() == 1);
    CHECK(bed.stack.Top() == b.Get());
    // a was detached from the root; b is attached.
    CHECK(a->Parent == nullptr);
    CHECK(b->Parent == bed.root.Get());
}

TEST_CASE("screenstack: clear empties the stack")
{
    Bed bed;
    bed.stack.Push(MakeScreen());
    bed.stack.Push(MakeScreen());
    bed.stack.Push(MakeScreen());
    CHECK(bed.stack.Count() == 3);
    bed.stack.Clear();
    CHECK(bed.stack.Count() == 0);
    CHECK(bed.stack.Top() == nullptr);
}

TEST_CASE("screenstack: an Opaque screen hides those below; popping it reveals them")
{
    Bed bed;
    auto hud = MakeScreen(ScreenMode::Overlay);
    auto menu = MakeScreen(ScreenMode::Opaque);
    bed.stack.Push(hud);
    CHECK(hud->Visibility == Visibility::Visible);

    bed.stack.Push(menu);
    CHECK(menu->Visibility == Visibility::Visible);
    CHECK(hud->Visibility == Visibility::Hidden); // covered by the opaque menu

    bed.stack.Pop();
    CHECK(hud->Visibility == Visibility::Visible); // revealed again
}

TEST_CASE("screenstack: a Modal screen does NOT hide those below (only shields input)")
{
    Bed bed;
    auto hud = MakeScreen(ScreenMode::Overlay);
    auto dialog = MakeScreen(ScreenMode::Modal);
    bed.stack.Push(hud);
    bed.stack.Push(dialog);
    CHECK(hud->Visibility == Visibility::Visible);   // still visible behind the modal
    CHECK(dialog->IsHitTestVisible == true);          // modal shields input below
    CHECK(hud->IsHitTestVisible == false);            // overlay passes input through
}

TEST_CASE("screenstack: lifecycle hooks fire on push/pop/cover/uncover")
{
    Bed bed;
    auto a = MakeScreen();
    auto b = MakeScreen();

    bed.stack.Push(a);
    CHECK(a->enter == 1);
    CHECK(a->shown == 1);

    bed.stack.Push(b); // b enters+shows; a is hidden (covered)
    CHECK(b->enter == 1);
    CHECK(b->shown == 1);
    CHECK(a->hidden == 1);

    bed.stack.Pop(); // b exits; a shows again (uncovered)
    CHECK(b->exit == 1);
    CHECK(a->shown == 2);
}

TEST_CASE("screenstack: focus is saved on push and restored on pop")
{
    Bed bed;
    FocusManager* fm = bed.context.GetFocusManager();
    REQUIRE(fm != nullptr);

    auto a = MakeScreen();
    a->AddButton(u8"aBtn");
    bed.stack.Push(a); // push focuses a's first focusable (aBtn)
    CHECK(fm->FocusedView() == a->button.Get());

    auto b = MakeScreen();
    b->AddButton(u8"bBtn");
    bed.stack.Push(b); // saves aBtn, focuses bBtn
    CHECK(fm->FocusedView() == b->button.Get());

    bed.stack.Pop(); // restores aBtn
    CHECK(fm->FocusedView() == a->button.Get());
}

TEST_CASE("screenstack: Back pops the top unless it is the last screen")
{
    Bed bed;
    auto a = MakeScreen();
    auto b = MakeScreen();
    bed.stack.Push(a);
    CHECK(bed.stack.HandleBack() == false); // never pop the last screen out from under the player
    CHECK(bed.stack.Count() == 1);

    bed.stack.Push(b);
    CHECK(bed.stack.HandleBack() == true); // pops b
    CHECK(bed.stack.Count() == 1);
    CHECK(bed.stack.Top() == a.Get());
}

TEST_CASE("screenstack: a transition-pop removal defers through the mutation queue (no re-entrant "
          "AnimationManager crash)")
{
    Bed bed;
    auto a = MakeScreen();
    auto b = MakeScreen();
    b->SetTransition(TransitionDesc{TransitionKind::Scale, 0.1f}); // scale + fade on push/pop
    bed.stack.Push(a);
    bed.stack.Push(b);
    CHECK(bed.stack.Count() == 2);

    // Let b's IN-transition finish (BeginFrame drives the AnimationManager).
    for (int i = 0; i < 4; ++i)
    {
        bed.context.BeginFrame(0.1f);
    }

    // Pop b: plays the Scale OUT-transition, then removes b on completion. That removal must route
    // through the mutation queue - BeginFrame ticks animations under a NON-Idle phase, so the
    // onComplete's RemoveView defers instead of running inline (an inline RemoveView would re-enter
    // AnimationManager::Update -> CancelForView -> RemoveAtSwap on the array under iteration = crash).
    CHECK(b->exit == 0);
    bed.stack.Pop();
    CHECK(bed.stack.Count() == 1); // bookkeeping is synchronous

    // Frame N: the out-transition completes -> the removal QUEUES. Frame N+1: the drain removes b.
    for (int i = 0; i < 4; ++i)
    {
        bed.context.BeginFrame(0.1f);
    }
    CHECK(b->exit == 1); // OnExit fired = b was actually removed (no crash)
    CHECK(bed.stack.Top() == a.Get());
}
