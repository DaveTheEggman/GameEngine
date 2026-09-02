// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI.Gamekit - ToastHost behavior (native, backend-neutral). Copied from the toolkit; these mirror
// the behaviour contract: Show tracks a card, timed toasts expire on Update, sticky toasts persist
// until Dismiss, and an action toast fires its callback (then closes).
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
    struct ToastBed
    {
        UIContext context{DefaultAllocator()};
        RefPtr<RootView> root;
        RefPtr<ToastHost> host;
        ToastBed()
        {
            root = MakeRef<RootView>(DefaultAllocator());
            context.AddRootView(root.Get());
            host = MakeRef<ToastHost>(DefaultAllocator());
            root->AddView(host.Get());
        }
    };
}

TEST_CASE("toast: Show adds a card; ToastCount + Contains track it")
{
    ToastBed bed;
    CHECK(bed.host->ToastCount() == 0);

    ToastRequest req;
    req.message = String(u8"Saved");
    const u64 id = bed.host->Show(Move(req));

    CHECK(bed.host->ToastCount() == 1);
    CHECK(bed.host->Contains(id));
    CHECK(bed.host->ChildCount() == 1); // the card view
}

TEST_CASE("toast: a timed toast expires after its duration on Update")
{
    ToastBed bed;
    ToastRequest req;
    req.message = String(u8"Hi");
    req.durationSeconds = 1.0f;
    const u64 id = bed.host->Show(Move(req));

    bed.host->Update(0.5f);
    CHECK(bed.host->Contains(id)); // still alive mid-duration

    bed.host->Update(0.6f); // total 1.1s > 1.0s -> expires + removed
    CHECK_FALSE(bed.host->Contains(id));
    CHECK(bed.host->ToastCount() == 0);
    CHECK(bed.host->ChildCount() == 0);
}

TEST_CASE("toast: a sticky toast (duration <= 0) persists until Dismiss")
{
    ToastBed bed;
    ToastRequest req;
    req.message = String(u8"Error");
    req.severity = ToastSeverity::Error;
    req.durationSeconds = 0.0f; // sticky
    const u64 id = bed.host->Show(Move(req));

    bed.host->Update(100.0f); // ages a lot; a sticky toast ignores time
    CHECK(bed.host->Contains(id));

    bed.host->Dismiss(id);
    bed.host->Update(0.0f); // removed on the next Update
    CHECK_FALSE(bed.host->Contains(id));
}

TEST_CASE("toast: an action toast fires its callback and then closes")
{
    ToastBed bed;
    bool fired = false;
    ToastRequest req;
    req.message = String(u8"Deleted");
    req.durationSeconds = 0.0f;
    req.actionLabel = String(u8"Undo");
    req.onAction = [&fired]() { fired = true; };
    const u64 id = bed.host->Show(Move(req));

    // The card lays out as [message, action button, close button].
    ViewGroup* card = Cast<ViewGroup>(bed.host->GetChildAt(0));
    REQUIRE(card != nullptr);
    REQUIRE(card->ChildCount() == 3);
    ButtonBase* action = Cast<ButtonBase>(card->GetChildAt(1));
    REQUIRE(action != nullptr);

    action->FireClick();
    CHECK(fired);

    bed.host->Update(0.0f); // the action marked the toast closing -> removed
    CHECK_FALSE(bed.host->Contains(id));
}
