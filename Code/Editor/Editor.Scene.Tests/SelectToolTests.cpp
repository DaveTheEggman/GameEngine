// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// SelectTransformTool tests: the default viewport tool preserves the pre-framework page
// behavior exactly - gizmo drags are one undo entry and consume the pointer (pick suppressed),
// click-pick selects/toggles/clears, editingLocked (Simulate) blocks drags but NOT selection,
// and OnDeactivate ends an in-flight drag with no half-applied command group.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.scene;
import editor.core;
import editor.scene;
import editor.viewporttools;

using namespace foundation::core;
using namespace editor;
namespace core = foundation::core;

namespace
{
    // The GizmoTests fixture camera: at +Z looking down -Z at the origin.
    constexpr Float3 kCamPos{0.0f, 0.0f, 10.0f};
    constexpr Float3 kCamFwd{0.0f, 0.0f, -1.0f};

    ViewportToolInput Frame(Float3 through, bool pressed, bool down, bool released,
                            bool ctrl = false)
    {
        ViewportToolInput in;
        in.ray.origin = kCamPos;
        in.ray.direction = Normalized(through - kCamPos);
        in.cameraPosition = kCamPos;
        in.cameraForward = kCamFwd;
        in.leftPressed = pressed;
        in.leftDown = down;
        in.leftReleased = released;
        in.ctrl = ctrl;
        return in;
    }

    struct Fixture
    {
        foundation::scene::Scene scene;
        EditorCommandStack commands;
        SceneEditContext edit;
        SelectTransformTool tool;

        Fixture() : edit(scene, commands), tool(edit) {}
    };
}

TEST_CASE("select-tool: identity and default availability")
{
    Fixture f;
    CHECK(f.tool.Id() == StringView(u8"select"));
    CHECK(f.tool.IsAvailable()); // the default tool must never report unavailable
}

TEST_CASE("select-tool: click picks the nearest entity, Ctrl toggles, empty space clears")
{
    Fixture f;
    const Guid nearId = f.edit.CreateEntity(u8"Near");
    const Guid farId = f.edit.CreateEntity(u8"Far");
    {
        core::Transform t;
        t.position = Float3{0.0f, 0.0f, -5.0f}; // behind Near along the same click ray
        f.scene.SetLocalTransform(f.edit.Resolve(farId), t);
    }
    Selection<Guid>& selection = f.edit.EntitySelection();
    // CreateEntity auto-selects (editor UX) - clear so the gizmo is INACTIVE and the first
    // click below reaches picking instead of the selected entity's center handle.
    selection.Clear();

    // Click through both entity centers: the NEARER one wins.
    CHECK(!f.tool.Update(Frame(Float3{}, true, true, false)));
    CHECK(selection.Contains(nearId));
    CHECK(!selection.Contains(farId));
    (void)f.tool.Update(Frame(Float3{}, false, false, true));

    // Ctrl-click far off to the side: nothing hit, Ctrl preserves the selection.
    CHECK(!f.tool.Update(Frame(Float3{50.0f, 0.0f, 0.0f}, true, true, false, true)));
    CHECK(selection.Contains(nearId));
    (void)f.tool.Update(Frame(Float3{50.0f, 0.0f, 0.0f}, false, false, true));

    // Plain click on empty space clears.
    CHECK(!f.tool.Update(Frame(Float3{50.0f, 0.0f, 0.0f}, true, true, false)));
    CHECK(!selection.Contains(nearId));

    // pointerOver=false: a press cannot pick (click-initiated gestures need the pointer OVER).
    (void)f.tool.Update(Frame(Float3{50.0f, 0.0f, 0.0f}, false, false, true));
    ViewportToolInput away = Frame(Float3{}, true, true, false);
    away.pointerOver = false;
    CHECK(!f.tool.Update(away));
    CHECK(!selection.Contains(nearId));
}

TEST_CASE("select-tool: a gizmo drag consumes the pointer, is one undo entry, and suppresses "
          "picking")
{
    Fixture f;
    const Guid boxId = f.edit.CreateEntity(u8"Box");
    const Guid otherId = f.edit.CreateEntity(u8"Other");
    {
        core::Transform t;
        t.position = Float3{20.0f, 0.0f, 0.0f}; // out of the way; must stay unselected
        f.scene.SetLocalTransform(f.edit.Resolve(otherId), t);
    }
    f.edit.EntitySelection().Set(boxId);
    f.commands.Clear(); // forget the creates so undo counts below are the drag only

    const f32 size = TransformGizmo::ScreenScale(kCamPos, kCamFwd, 1.0472f, Float3{});
    const Float3 grab{size * 0.6f, 0.0f, 0.0f};

    // Hover the X shaft: consumed (hot handle), no command, selection untouched.
    CHECK(f.tool.Update(Frame(grab, false, false, false)));
    CHECK_FALSE(f.commands.CanUndo());

    // Press-drag-release: +X by ~2, exactly one undo entry, restore on undo.
    CHECK(f.tool.Update(Frame(grab, true, true, false)));
    CHECK(f.tool.Update(Frame(grab + Float3{1.0f, 0, 0}, false, true, false)));
    CHECK(f.tool.Update(Frame(grab + Float3{2.0f, 0, 0}, false, true, false)));
    CHECK(f.tool.Update(Frame(grab + Float3{2.0f, 0, 0}, false, false, true)));

    CHECK(f.scene.GetLocalTransform(f.edit.Resolve(boxId)).position.x ==
          doctest::Approx(2.0f).epsilon(0.05f));
    CHECK(f.edit.EntitySelection().Contains(boxId)); // the consumed press never picked
    REQUIRE(f.commands.CanUndo());
    f.commands.Undo();
    CHECK(f.scene.GetLocalTransform(f.edit.Resolve(boxId)).position.x == doctest::Approx(0.0f));
    CHECK_FALSE(f.commands.CanUndo());
}

TEST_CASE("select-tool: editingLocked blocks drags but selection picking still works")
{
    Fixture f;
    const Guid boxId = f.edit.CreateEntity(u8"Box");
    f.edit.EntitySelection().Set(boxId);
    f.commands.Clear();

    const f32 size = TransformGizmo::ScreenScale(kCamPos, kCamFwd, 1.0472f, Float3{});
    const Float3 grab{size * 0.6f, 0.0f, 0.0f};

    // A press on the X shaft with editing locked: the gizmo is pointer-less (no hot handle, no
    // drag, no command). The press falls through to PICKING - the ray through the shaft misses
    // the entity's pick sphere, so the selection CLEARS (selecting is not an edit).
    ViewportToolInput locked = Frame(grab, true, true, false);
    locked.editingLocked = true;
    CHECK(!f.tool.Update(locked));
    CHECK_FALSE(f.commands.CanUndo());
    CHECK(!f.edit.EntitySelection().Contains(boxId));

    // Clicking the entity itself while locked selects it again.
    ViewportToolInput pick = Frame(Float3{}, true, true, false);
    pick.editingLocked = true;
    CHECK(!f.tool.Update(pick));
    CHECK(f.edit.EntitySelection().Contains(boxId));
    CHECK(f.scene.GetLocalTransform(f.edit.Resolve(boxId)).position.x == doctest::Approx(0.0f));
}

TEST_CASE("select-tool: OnDeactivate ends an in-flight drag - no half-applied command group")
{
    Fixture f;
    const Guid boxId = f.edit.CreateEntity(u8"Box");
    f.edit.EntitySelection().Set(boxId);
    f.commands.Clear();

    const f32 size = TransformGizmo::ScreenScale(kCamPos, kCamFwd, 1.0472f, Float3{});
    const Float3 grab{size * 0.6f, 0.0f, 0.0f};

    CHECK(f.tool.Update(Frame(grab, false, false, false)));
    CHECK(f.tool.Update(Frame(grab, true, true, false)));
    CHECK(f.tool.Update(Frame(grab + Float3{1.0f, 0, 0}, false, true, false)));

    f.tool.OnDeactivate(); // tool switch mid-drag

    // The gesture is closed: whatever it left is a COMPLETE undo unit - undo restores the exact
    // start transform and the stack is empty after (never a dangling open group).
    if (f.commands.CanUndo())
    {
        f.commands.Undo();
    }
    CHECK(f.scene.GetLocalTransform(f.edit.Resolve(boxId)).position.x == doctest::Approx(0.0f));
    CHECK_FALSE(f.commands.CanUndo());
}
