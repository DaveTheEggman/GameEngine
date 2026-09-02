// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The collision-groups matrix editor (physics scene settings) must REBUILD its grid when the group
// list is mutated - the smoke-test bug was "UI doesn't update when a group is added". The rebuild is
// mutation-queue-deferred in the live editor; a bare editor (no UIContext attached) rebuilds inline,
// which is exactly what this asserts.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.scene;
import engine.physics;
import editor.core;
import editor.scene;
import foundation.ui;
import foundation.ui.toolkit;

using namespace foundation::core;
namespace scene = foundation::scene;

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

// === The REAL delete handler (BuildCollisionMatrixRow's OnRemoveGroup lambda) ===
//
// The handler lives in InspectorViewImpl.cpp, wired onto the CollisionMatrixEditor when the
// inspector's Scene tab builds. Building a real SceneInspectorView over a scene with a
// PhysicsSceneSystem reaches it headlessly: the Scene tab's grid holds the matrix editor, and
// its OnRemoveGroup IS the production lambda (guards + column-bit clearing + undoable commit).

namespace
{
    // Seeds a 3-group matrix with cross bits on the LIVE settings, builds the inspector's Scene
    // tab, and returns the wired matrix editor (owned by the inspector's grid).
    editor::CollisionMatrixEditor* BuildSceneTabMatrix(editor::SceneInspectorView& inspector)
    {
        auto* tabs = Cast<foundation::ui::TabView>(inspector.GetChildAt(0));
        if (tabs == nullptr)
        {
            return nullptr;
        }
        tabs->SetSelectedIndex(1); // the Scene tab (scene-settings grid)
        inspector.Refresh();
        return Cast<editor::CollisionMatrixEditor>(
            inspector.Grid()->GetProperty(u8"Collision Groups"));
    }
}

TEST_CASE("inspector: removing the last collision group clears its bit from every surviving mask")
{
    engine::physics::RegisterPhysicsComponentReflection();

    scene::Scene scene(u8"t");
    auto* physics = scene.AddSystem<engine::physics::PhysicsSceneSystem>();
    auto& settings = physics->Settings();
    settings.groupNames.PushBack(String(u8"Default"));
    settings.groupNames.PushBack(String(u8"Player"));
    settings.groupNames.PushBack(String(u8"Debris"));
    settings.groupCollides.PushBack(0b101u); // Default: vs Default + Debris
    settings.groupCollides.PushBack(0b110u); // Player:  vs Player + Debris
    settings.groupCollides.PushBack(0b111u); // Debris:  vs everyone

    editor::EditorCommandStack commands;
    editor::SceneEditContext edit(scene, commands);
    editor::EditorContext editorContext;
    auto inspectorRef =
        foundation::core::MakeRef<editor::SceneInspectorView>(DefaultAllocator(), editorContext, edit);
    editor::SceneInspectorView& inspector = *inspectorRef;

    editor::CollisionMatrixEditor* matrix = BuildSceneTabMatrix(inspector);
    REQUIRE(matrix != nullptr);
    REQUIRE(matrix->names.Size() == 3);
    REQUIRE(bool(matrix->OnRemoveGroup)); // the production handler is wired

    matrix->OnRemoveGroup(2); // remove the LAST group ("Debris")

    // The group is gone and its column bit (bit 2) is cleared from every surviving row.
    REQUIRE(matrix->names.Size() == 2);
    REQUIRE(matrix->matrix.Size() == 2);
    for (u32 mask : matrix->matrix)
    {
        CHECK((mask & (1u << 2)) == 0u);
    }
    CHECK(matrix->matrix[0] == 0b001u); // Default kept only its non-Debris bits
    CHECK(matrix->matrix[1] == 0b010u); // Player likewise

    // The commit reached the LIVE settings block through the undoable command.
    REQUIRE(settings.groupCollides.Size() == 2);
    CHECK(settings.groupNames.Size() == 2);
    CHECK(settings.groupCollides[0] == 0b001u);
    CHECK(settings.groupCollides[1] == 0b010u);
}

TEST_CASE("inspector: collision group removal refuses non-last indices and the final group")
{
    engine::physics::RegisterPhysicsComponentReflection();

    scene::Scene scene(u8"t");
    auto* physics = scene.AddSystem<engine::physics::PhysicsSceneSystem>();
    auto& settings = physics->Settings();
    settings.groupNames.PushBack(String(u8"Default"));
    settings.groupNames.PushBack(String(u8"Player"));
    settings.groupNames.PushBack(String(u8"Debris"));
    settings.groupCollides.PushBack(0b111u);
    settings.groupCollides.PushBack(0b111u);
    settings.groupCollides.PushBack(0b111u);

    editor::EditorCommandStack commands;
    editor::SceneEditContext edit(scene, commands);
    editor::EditorContext editorContext;
    auto inspectorRef =
        foundation::core::MakeRef<editor::SceneInspectorView>(DefaultAllocator(), editorContext, edit);
    editor::SceneInspectorView& inspector = *inspectorRef;

    editor::CollisionMatrixEditor* matrix = BuildSceneTabMatrix(inspector);
    REQUIRE(matrix != nullptr);
    REQUIRE(matrix->names.Size() == 3);

    // A NON-LAST index is refused (a mid-list delete would renumber higher groups).
    matrix->OnRemoveGroup(0);
    CHECK(matrix->names.Size() == 3);
    CHECK(matrix->matrix.Size() == 3);
    CHECK(matrix->matrix[0] == 0b111u); // untouched

    // An out-of-range index is refused too.
    matrix->OnRemoveGroup(7);
    CHECK(matrix->names.Size() == 3);

    // Popping the tail twice is allowed...
    matrix->OnRemoveGroup(2);
    CHECK(matrix->names.Size() == 2);
    matrix->OnRemoveGroup(1);
    REQUIRE(matrix->names.Size() == 1);

    // ...but never below ONE group.
    matrix->OnRemoveGroup(0);
    CHECK(matrix->names.Size() == 1);
    CHECK(matrix->matrix.Size() == 1);
}
