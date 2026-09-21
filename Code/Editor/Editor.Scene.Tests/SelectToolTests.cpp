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
        foundation::scene::Scene scene{DefaultAllocator()};
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

namespace
{
    // A host picker that records requests and answers when the test says so.
    class FakePicker final : public IViewportPicker
    {
    public:
        u32 RequestPick(i32 x, i32 y, u32 width, u32 height) override
        {
            ++requests;
            lastX = x;
            lastY = y;
            lastW = width;
            lastH = height;
            return refuse ? 0u : nextId++;
        }
        bool TryTakePick(u32 request, Array<foundation::scene::EntityHandle>& hits) override
        {
            if (!answerReady || request != answerFor)
            {
                return false;
            }
            hits = Move(answer);
            answerReady = false;
            return true;
        }
        void Answer(u32 request, Array<foundation::scene::EntityHandle> hits)
        {
            answerFor = request;
            answer = Move(hits);
            answerReady = true;
        }
        u32 requests = 0;
        i32 lastX = -1, lastY = -1;
        u32 lastW = 0, lastH = 0;
        u32 nextId = 1;
        bool refuse = false;

    private:
        u32 answerFor = 0;
        bool answerReady = false;
        Array<foundation::scene::EntityHandle> answer;
    };

    ViewportToolInput PixelFrame(Float3 through, bool pressed, i32 px, i32 py, bool ctrl = false)
    {
        ViewportToolInput in = Frame(through, pressed, pressed, !pressed, ctrl);
        in.pointerX = px;
        in.pointerY = py;
        in.viewportWidth = 640;
        in.viewportHeight = 360;
        return in;
    }
}

TEST_CASE("select-tool: with a picker a click asks the GPU for the pointer pixel and applies the answer")
{
    Fixture f;
    FakePicker picker;
    f.tool.SetPicker(&picker);
    const Guid nearId = f.edit.CreateEntity(u8"Near");
    const Guid farId = f.edit.CreateEntity(u8"Far");
    {
        core::Transform t;
        t.position = Float3{0.0f, 0.0f, -5.0f};
        f.scene.SetLocalTransform(f.edit.Resolve(farId), t);
    }
    Selection<Guid>& selection = f.edit.EntitySelection();
    selection.Clear();

    // Press: a 1x1 request at the pointer pixel; nothing selected until the answer lands.
    CHECK(!f.tool.Update(PixelFrame(Float3{}, true, 123, 45)));
    CHECK(picker.requests == 1u);
    CHECK(picker.lastX == 123);
    CHECK(picker.lastY == 45);
    CHECK(picker.lastW == 1u);
    CHECK(picker.lastH == 1u);
    CHECK(f.tool.HasPendingPick());
    CHECK(selection.IsEmpty());

    // The GPU saw the FAR entity's surface (it is what is drawn under the pointer, whatever the
    // CPU origin pick would say): the answer wins.
    Array<foundation::scene::EntityHandle> hits;
    hits.PushBack(f.edit.Resolve(farId));
    picker.Answer(1, Move(hits));
    (void)f.tool.Update(PixelFrame(Float3{}, false, 123, 45));
    CHECK_FALSE(f.tool.HasPendingPick());
    CHECK(selection.Contains(farId));
    CHECK(!selection.Contains(nearId));
}

TEST_CASE("select-tool: a GPU miss falls back to the CPU pick; Ctrl rides along; newest click wins")
{
    Fixture f;
    FakePicker picker;
    f.tool.SetPicker(&picker);
    const Guid nearId = f.edit.CreateEntity(u8"Near");
    const Guid farId = f.edit.CreateEntity(u8"Far");
    {
        core::Transform t;
        t.position = Float3{0.0f, 0.0f, -5.0f};
        f.scene.SetLocalTransform(f.edit.Resolve(farId), t);
    }
    Selection<Guid>& selection = f.edit.EntitySelection();
    selection.Clear();

    // Click through both origins: the GPU answers "nothing drawn there" (e.g. an empty or a
    // light) -> the CPU origin pick's nearest entity is selected.
    CHECK(!f.tool.Update(PixelFrame(Float3{}, true, 10, 10)));
    picker.Answer(1, Array<foundation::scene::EntityHandle>{});
    (void)f.tool.Update(PixelFrame(Float3{}, false, 10, 10));
    CHECK(selection.Contains(nearId));
    CHECK(!selection.Contains(farId));

    // Ctrl-click answered with the far entity: toggled INTO the selection (Ctrl was captured
    // with the click, not read at answer time). The current selection is an entity OFF the
    // click ray, so its gizmo does not consume the press (the gizmo-first rule is unchanged).
    (void)f.tool.Update(PixelFrame(Float3{}, false, 10, 10));
    const Guid asideId = f.edit.CreateEntity(u8"Aside");
    {
        core::Transform t;
        t.position = Float3{20.0f, 0.0f, 0.0f};
        f.scene.SetLocalTransform(f.edit.Resolve(asideId), t);
    }
    selection.Set(asideId);
    CHECK(!f.tool.Update(PixelFrame(Float3{}, true, 10, 10, /*ctrl*/ true)));
    {
        Array<foundation::scene::EntityHandle> hits;
        hits.PushBack(f.edit.Resolve(farId));
        picker.Answer(2, Move(hits));
    }
    (void)f.tool.Update(PixelFrame(Float3{}, false, 10, 10)); // no Ctrl now
    CHECK(selection.Contains(asideId));
    CHECK(selection.Contains(farId));
    CHECK(!selection.Contains(nearId));

    // Two clicks before any answer: the FIRST request's answer is ignored, the second applies.
    selection.Clear();
    CHECK(!f.tool.Update(PixelFrame(Float3{}, true, 10, 10)));
    (void)f.tool.Update(PixelFrame(Float3{}, false, 10, 10));
    CHECK(!f.tool.Update(PixelFrame(Float3{}, true, 11, 11)));
    CHECK(picker.requests == 4u);
    {
        Array<foundation::scene::EntityHandle> hits;
        hits.PushBack(f.edit.Resolve(farId));
        picker.Answer(3, Move(hits)); // the stale one
    }
    (void)f.tool.Update(PixelFrame(Float3{}, false, 11, 11));
    CHECK(selection.IsEmpty());
    CHECK(f.tool.HasPendingPick());
    {
        Array<foundation::scene::EntityHandle> hits;
        hits.PushBack(f.edit.Resolve(nearId));
        picker.Answer(4, Move(hits));
    }
    (void)f.tool.Update(PixelFrame(Float3{}, false, 11, 11));
    CHECK(selection.Contains(nearId));
    CHECK(!selection.Contains(farId));

    // A dead handle in the answer (entity destroyed while the pick was in flight) is skipped:
    // the CPU answer stands.
    selection.Clear();
    const foundation::scene::EntityHandle farHandle = f.edit.Resolve(farId);
    CHECK(!f.tool.Update(PixelFrame(Float3{}, true, 10, 10)));
    f.scene.DestroyEntity(farHandle);
    {
        Array<foundation::scene::EntityHandle> hits;
        hits.PushBack(farHandle);
        picker.Answer(5, Move(hits));
    }
    (void)f.tool.Update(PixelFrame(Float3{}, false, 10, 10));
    CHECK(selection.Contains(nearId));
}

TEST_CASE("select-tool: no pixel position, or a picker that refuses, picks on the CPU at once")
{
    Fixture f;
    FakePicker picker;
    f.tool.SetPicker(&picker);
    const Guid nearId = f.edit.CreateEntity(u8"Near");
    Selection<Guid>& selection = f.edit.EntitySelection();
    selection.Clear();

    // No viewport size on the input (a host without pixels): immediate CPU pick, no request.
    CHECK(!f.tool.Update(Frame(Float3{}, true, true, false)));
    CHECK(picker.requests == 0u);
    CHECK(selection.Contains(nearId));
    (void)f.tool.Update(Frame(Float3{}, false, false, true));

    // The picker refuses (renderer not ready): immediate CPU pick.
    selection.Clear();
    picker.refuse = true;
    CHECK(!f.tool.Update(PixelFrame(Float3{}, true, 5, 5)));
    CHECK(picker.requests == 1u);
    CHECK_FALSE(f.tool.HasPendingPick());
    CHECK(selection.Contains(nearId));

    // A pointer outside the view never asks the GPU (selection cleared first: a selected
    // entity under the ray would hand the press to its gizmo).
    picker.refuse = false;
    (void)f.tool.Update(PixelFrame(Float3{}, false, 5, 5));
    selection.Clear();
    CHECK(!f.tool.Update(PixelFrame(Float3{}, true, 640, 5)));
    CHECK(picker.requests == 1u);
    CHECK(selection.Contains(nearId)); // the CPU pick answered instead
}

namespace
{
    // A frame mid-drag: button held, pointer at (px, py).
    ViewportToolInput DragFrame(Float3 through, i32 px, i32 py, bool ctrl = false)
    {
        ViewportToolInput in = Frame(through, false, true, false, ctrl);
        in.pointerX = px;
        in.pointerY = py;
        in.viewportWidth = 640;
        in.viewportHeight = 360;
        return in;
    }
    ViewportToolInput ReleaseFrame(Float3 through, i32 px, i32 py, bool ctrl = false)
    {
        ViewportToolInput in = Frame(through, false, false, true, ctrl);
        in.pointerX = px;
        in.pointerY = py;
        in.viewportWidth = 640;
        in.viewportHeight = 360;
        return in;
    }
    // Where a world point lands in the 640x360 fixture view (the tool's CPU projection).
    void FixturePixel(Float3 world, i32& px, i32& py)
    {
        const Float3 d = world - kCamPos;
        const f32 z = Dot(d, kCamFwd);
        const f32 tanY = Tan(1.0472f * 0.5f);
        const f32 tanX = tanY * (640.0f / 360.0f);
        px = static_cast<i32>((Dot(d, Float3{1, 0, 0}) / (z * tanX) * 0.5f + 0.5f) * 640.0f);
        py = static_cast<i32>((0.5f - Dot(d, Float3{0, 1, 0}) / (z * tanY) * 0.5f) * 360.0f);
    }
}

TEST_CASE("select-tool: a drag past the threshold becomes a marquee rect pick; the click is undone")
{
    Fixture f;
    FakePicker picker;
    f.tool.SetPicker(&picker);
    const Guid nearId = f.edit.CreateEntity(u8"Near");
    const Guid farId = f.edit.CreateEntity(u8"Far");
    {
        core::Transform t;
        t.position = Float3{0.0f, 0.0f, -5.0f};
        f.scene.SetLocalTransform(f.edit.Resolve(farId), t);
    }
    Selection<Guid>& selection = f.edit.EntitySelection();
    selection.Clear();

    // Press on empty space far to the side: the click asks the GPU (request 1)...
    const Float3 empty{50.0f, 0.0f, 0.0f};
    CHECK(!f.tool.Update(PixelFrame(empty, true, 100, 80)));
    CHECK(f.tool.HasPendingPick());
    CHECK(!f.tool.IsMarqueeActive());
    // ...a 2-pixel wobble stays a click...
    CHECK(!f.tool.Update(DragFrame(empty, 102, 81)));
    CHECK(!f.tool.IsMarqueeActive());
    CHECK(f.tool.HasPendingPick());
    // ...past the threshold it is a marquee: the click's pick is dropped, the pointer consumed.
    CHECK(f.tool.Update(DragFrame(empty, 160, 140)));
    CHECK(f.tool.IsMarqueeActive());
    CHECK_FALSE(f.tool.HasPendingPick());
    CHECK(picker.requests == 1u);

    // Release: ONE rect request over the dragged pixels (inclusive), any corner order.
    (void)f.tool.Update(ReleaseFrame(empty, 40, 140));
    CHECK(!f.tool.IsMarqueeActive());
    CHECK(f.tool.HasPendingMarquee());
    CHECK(picker.requests == 2u);
    CHECK(picker.lastX == 40);
    CHECK(picker.lastY == 80);
    CHECK(picker.lastW == 61u);
    CHECK(picker.lastH == 61u);
    CHECK(selection.IsEmpty()); // nothing until the answer lands

    // The answer: both entities drawn in the rect -> both selected; a dead handle is skipped.
    Array<foundation::scene::EntityHandle> hits;
    hits.PushBack(f.edit.Resolve(nearId));
    hits.PushBack(f.edit.Resolve(farId));
    hits.PushBack(foundation::scene::EntityHandle{999u, 7u});
    picker.Answer(2, Move(hits));
    (void)f.tool.Update(ReleaseFrame(empty, 40, 140));
    CHECK_FALSE(f.tool.HasPendingMarquee());
    CHECK(selection.Contains(nearId));
    CHECK(selection.Contains(farId));
    CHECK(selection.Items().Size() == 2);

    // A marquee that answers nothing clears (plain), the stale click's answer never applies.
    CHECK(!f.tool.Update(PixelFrame(empty, true, 300, 300)));
    (void)f.tool.Update(DragFrame(empty, 340, 340));
    (void)f.tool.Update(ReleaseFrame(empty, 340, 340));
    picker.Answer(3, Array<foundation::scene::EntityHandle>{}); // the click's id: ignored
    (void)f.tool.Update(ReleaseFrame(empty, 340, 340));
    CHECK(selection.Items().Size() == 2);
    picker.Answer(4, Array<foundation::scene::EntityHandle>{}); // the rect's id
    (void)f.tool.Update(ReleaseFrame(empty, 340, 340));
    CHECK(selection.IsEmpty());
}

TEST_CASE("select-tool: Ctrl-marquee adds to the selection and the press's click is restored")
{
    Fixture f;
    FakePicker picker;
    f.tool.SetPicker(&picker);
    const Guid nearId = f.edit.CreateEntity(u8"Near");
    const Guid farId = f.edit.CreateEntity(u8"Far");
    const Guid asideId = f.edit.CreateEntity(u8"Aside");
    {
        core::Transform t;
        t.position = Float3{0.0f, 0.0f, -5.0f};
        f.scene.SetLocalTransform(f.edit.Resolve(farId), t);
        t.position = Float3{20.0f, 0.0f, 0.0f};
        f.scene.SetLocalTransform(f.edit.Resolve(asideId), t);
    }
    Selection<Guid>& selection = f.edit.EntitySelection();
    selection.Set(asideId);

    const Float3 empty{50.0f, 0.0f, 0.0f};
    CHECK(!f.tool.Update(PixelFrame(empty, true, 10, 10, /*ctrl*/ true)));
    (void)f.tool.Update(DragFrame(empty, 90, 90, true));
    (void)f.tool.Update(ReleaseFrame(empty, 90, 90)); // Ctrl captured at press, not here
    Array<foundation::scene::EntityHandle> hits;
    hits.PushBack(f.edit.Resolve(nearId));
    hits.PushBack(f.edit.Resolve(farId));
    picker.Answer(2, Move(hits));
    (void)f.tool.Update(ReleaseFrame(empty, 90, 90));
    CHECK(selection.Contains(asideId));
    CHECK(selection.Contains(nearId));
    CHECK(selection.Contains(farId));
    CHECK(selection.Items().Size() == 3);

    // Plain (no Ctrl) press on empty space with a synchronous CPU click (picker refuses): the
    // click CLEARS the selection; dragging into a marquee puts it back first, then the rect's
    // answer replaces it.
    picker.refuse = true;
    CHECK(!f.tool.Update(PixelFrame(empty, true, 200, 200)));
    CHECK(selection.IsEmpty()); // the click's CPU answer: nothing there
    (void)f.tool.Update(DragFrame(empty, 260, 260));
    CHECK(selection.Items().Size() == 3); // restored while the marquee is dragged
    picker.refuse = false;
    (void)f.tool.Update(ReleaseFrame(empty, 260, 260));
    Array<foundation::scene::EntityHandle> one;
    one.PushBack(f.edit.Resolve(farId));
    picker.Answer(picker.nextId - 1, Move(one));
    (void)f.tool.Update(ReleaseFrame(empty, 260, 260));
    CHECK(selection.Items().Size() == 1);
    CHECK(selection.Contains(farId));
}

TEST_CASE("select-tool: without a picker the marquee selects the origins projecting inside it")
{
    Fixture f; // no picker
    const Guid nearId = f.edit.CreateEntity(u8"Near");
    const Guid farId = f.edit.CreateEntity(u8"Far");
    const Guid asideId = f.edit.CreateEntity(u8"Aside");
    const Guid behindId = f.edit.CreateEntity(u8"Behind");
    {
        core::Transform t;
        t.position = Float3{0.0f, 0.0f, -5.0f};
        f.scene.SetLocalTransform(f.edit.Resolve(farId), t);
        t.position = Float3{3.0f, 1.0f, 0.0f};
        f.scene.SetLocalTransform(f.edit.Resolve(asideId), t);
        t.position = Float3{0.0f, 0.0f, 20.0f}; // behind the camera
        f.scene.SetLocalTransform(f.edit.Resolve(behindId), t);
    }
    f.scene.UpdateTransforms(); // the CPU marquee reads world matrices
    Selection<Guid>& selection = f.edit.EntitySelection();
    selection.Clear();
    const Float3 empty{50.0f, 0.0f, 0.0f};

    // The whole view: everything in front of the camera.
    CHECK(!f.tool.Update(PixelFrame(empty, true, 0, 0)));
    (void)f.tool.Update(DragFrame(empty, 639, 359));
    (void)f.tool.Update(ReleaseFrame(empty, 639, 359));
    CHECK(selection.Contains(nearId));
    CHECK(selection.Contains(farId));
    CHECK(selection.Contains(asideId));
    CHECK(!selection.Contains(behindId));
    CHECK(selection.Items().Size() == 3);

    // A tight rect around the aside entity's pixel: only it.
    i32 ax = 0, ay = 0;
    FixturePixel(Float3{3.0f, 1.0f, 0.0f}, ax, ay);
    CHECK(!f.tool.Update(PixelFrame(empty, true, ax - 6, ay - 6)));
    (void)f.tool.Update(DragFrame(empty, ax + 6, ay + 6));
    (void)f.tool.Update(ReleaseFrame(empty, ax + 6, ay + 6));
    CHECK(selection.Items().Size() == 1);
    CHECK(selection.Contains(asideId));

    // OnDeactivate mid-drag drops the marquee: nothing applied, selection untouched.
    CHECK(!f.tool.Update(PixelFrame(empty, true, 0, 0)));
    (void)f.tool.Update(DragFrame(empty, 639, 359));
    CHECK(f.tool.IsMarqueeActive());
    f.tool.OnDeactivate();
    CHECK(!f.tool.IsMarqueeActive());
    (void)f.tool.Update(ReleaseFrame(empty, 639, 359));
    CHECK(selection.Items().Size() == 1);
    CHECK(selection.Contains(asideId));
}
