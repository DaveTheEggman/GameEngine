// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// EditorIcons: one slot table for Initialize's products, Bakeable and Shutdown. Regression for
// 2026-09-21: a hand-written Shutdown list missed the `close` icon, so its drawable was released
// by the singleton's destructor at process exit, after the allocator on main's stack was gone
// (ASan stack-use-after-return on every editor exit).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include <initializer_list>

import foundation.core;
import foundation.ui;
import editor.core;
import editor.app;

using namespace foundation::core;
using namespace editor;

TEST_CASE("editor-icons: Shutdown releases every slot Initialize filled, the close icon included")
{
    app::EditorIcons& icons = app::EditorIcons::Get();
    icons.Initialize();
    // Every slot baked: the table and the class agree on the count.
    CHECK(icons.Slots().Size() == app::EditorIcons::kSlotCount);
    CHECK(icons.Bakeable().Size() == app::EditorIcons::kSlotCount);
    CHECK(icons.close.Get() != nullptr);

    icons.Shutdown();
    CHECK(icons.Bakeable().IsEmpty());
    for (const RefPtr<foundation::ui::BakedSVGDrawable>* slot : icons.Slots())
    {
        CHECK(slot->Get() == nullptr);
    }
    CHECK(icons.close.Get() == nullptr);

    // Re-initialize works after a Shutdown (the app cycles it once; tests may twice).
    icons.Initialize();
    CHECK(icons.Bakeable().Size() == app::EditorIcons::kSlotCount);
    icons.Shutdown();
}
