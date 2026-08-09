// The collision-groups matrix editor (physics scene settings) must REBUILD its grid when the group
// list is mutated - the smoke-test bug was "UI doesn't update when a group is added". The rebuild is
// mutation-queue-deferred in the live editor; a bare editor (no UIContext attached) rebuilds inline,
// which is exactly what this asserts.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import editor.scene;
import foundation.ui;

using namespace foundation::core;

TEST_CASE("inspector: the collision-groups matrix rebuilds its grid when a group is added")
{
    // Heap-allocated (as PropertyEditors always are in the inspector): RequestRebuild takes a
    // RefPtr(this) to survive the deferred drain, so a stack instance would delete itself.
    RefPtr<editor::CollisionMatrixEditor> editor =
        MakeRef<editor::CollisionMatrixEditor>(DefaultAllocator(), u8"Collision Groups",
                                                         u8"Physics");
    editor->names.PushBack(String(u8"Default"));
    editor->matrix.PushBack(0xFFFFFFFFu);

    auto* grid = static_cast<foundation::ui::ViewGroup*>(editor->EditorView()); // a FlexLayout column
    REQUIRE(grid != nullptr);
    const usize before = grid->ChildCount(); // one group row + the "+ Add Group" button

    // Add Group: mutate the data, then request the rebuild (inline here - no UIContext attached).
    editor->names.PushBack(String(u8"Group 1"));
    editor->matrix.PushBack(0xFFFFFFFFu);
    editor->RequestRebuild();

    CHECK(grid->ChildCount() == before + 1); // the new row appeared - the grid was rebuilt

    // Removing back down rebuilds too (row count shrinks).
    editor->names.RemoveAt(1);
    editor->matrix.RemoveAt(1);
    editor->RequestRebuild();
    CHECK(grid->ChildCount() == before);
}
