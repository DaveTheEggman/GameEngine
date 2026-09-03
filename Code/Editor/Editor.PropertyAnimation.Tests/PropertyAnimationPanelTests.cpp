// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// PropertyAnimationPanel tests (the persistent in-scene
// editor). Covers the host-agnostic logic that does not need a live viewport: the reflected-type ->
// TrackValueKind mapping, the animatable-property collector (against a reflected test component), the
// panel acting as a clip-editor host (add-track routes through its command stack), the live-preview
// snapshot/restore (including the re-snapshot when the track set changes mid-preview and the
// EDIT-only gate), and the collapse toggle. The full add-from-selection scene walk + the on-screen
// docking are exercised by the build + manual UAT (they need a live scene / UI tree).

#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.scene;
import foundation.propertyanimation;
import editor.core;
import editor.propertyanimation;

using namespace foundation::core;
using namespace editor;

namespace propanim = foundation::propertyanimation;
namespace scene = foundation::scene;

// A plain VALUE component (REFLECT_VALUE patches TypeOf<PreviewComp>() directly, so its properties
// are visible through the manager's ComponentType() - an Object-derived type's TypeOf() would be the
// unpatched one and ResolveBinding would find nothing). Kept at file scope for the reflection macro.
struct PreviewComp
{
    Float3 position{0.0f, 0.0f, 0.0f};
};

REFLECT_VALUE(PreviewComp, "rtti::propanim::paneltest")
{
    builder.Property<&PreviewComp::position>("position");
}

namespace
{
    // A nested reflected struct (the color.tint path).
    class TestLight : public Object
    {
        RTTI_OBJECT(TestLight, Object)
    public:
        Color tint{1.0f, 1.0f, 1.0f, 1.0f};
    };

    // A component-like reflected object: animatable leaves (Float3/Quat/f32) + a nested Color +
    // a NON-animatable leaf (i32 flags, which the collector must skip).
    class TestComp : public Object
    {
        RTTI_OBJECT(TestComp, Object)
    public:
        Float3 position{0.0f, 0.0f, 0.0f};
        Quaternion rotation = Quaternion::Identity;
        f32 intensity = 0.0f;
        i32 flags = 0;
        TestLight light;
    };

    void EnsureRegistered()
    {
        static bool done = false;
        if (done)
        {
            return;
        }
        done = true;
        RegisterCoreTypes();
        GlobalTypeRegistry().Register(TestLight::StaticType());
        GlobalTypeRegistry().Register(TestComp::StaticType());
    }

    // Build a panel over a scene + fresh stack + selection (all owned by the caller; the panel
    // borrows). Preview/keying/seeding target the BOUND entity (workflow 2026-08-17), so bind
    // the pre-set selection here - the fixture equivalent of clicking "Use Selected".
    RefPtr<PropertyAnimationPanel> MakePanel(EditorContext& editorCtx, scene::Scene& sc,
                                             EditorCommandStack& stack, Selection<Guid>& selection)
    {
        auto panel =
            MakeRef<PropertyAnimationPanel>(DefaultAllocator(), editorCtx, sc, stack, selection);
        panel->BindSelectedEntity();
        return panel;
    }
}

REFLECT_MEMBERS(TestLight, "rtti::propanim::paneltest") { builder.Property<&TestLight::tint>("tint"); }

REFLECT_MEMBERS(TestComp, "rtti::propanim::paneltest")
{
    builder.Property<&TestComp::position>("position")
        .Property<&TestComp::rotation>("rotation")
        .Property<&TestComp::intensity>("intensity")
        .Property<&TestComp::flags>("flags")
        .Nested<&TestComp::light>("light");
}

TEST_CASE("propanim-panel: InferTrackKind maps the four animatable types, rejects others")
{
    CHECK(InferTrackKind(&TypeOf<f32>()).HasValue());
    CHECK(InferTrackKind(&TypeOf<f32>()).Value() == propanim::TrackValueKind::Float);
    CHECK(InferTrackKind(&TypeOf<Float3>()).Value() == propanim::TrackValueKind::Float3);
    CHECK(InferTrackKind(&TypeOf<Color>()).Value() == propanim::TrackValueKind::Color);
    CHECK(InferTrackKind(&TypeOf<Quaternion>()).Value() == propanim::TrackValueKind::Quat);
    CHECK_FALSE(InferTrackKind(&TypeOf<i32>()).HasValue());
    CHECK_FALSE(InferTrackKind(nullptr).HasValue());
}

TEST_CASE("propanim-panel: CollectAnimatableProperties seeds leaves + nested, skips non-animatable")
{
    EnsureRegistered();
    Array<AnimatablePropertyInfo> out;
    CollectAnimatableProperties(TestComp::StaticType(), out);

    // position (Float3), rotation (Quat), intensity (Float), light.tint (Color) - NOT flags (i32).
    CHECK(out.Size() == 4);

    bool sawPosition = false, sawRotation = false, sawIntensity = false, sawTint = false, sawFlags = false;
    for (const AnimatablePropertyInfo& p : out)
    {
        if (p.propertyPath.AsView() == StringView(u8"position"))
        {
            sawPosition = true;
            CHECK(p.kind == propanim::TrackValueKind::Float3);
        }
        else if (p.propertyPath.AsView() == StringView(u8"rotation"))
        {
            sawRotation = true;
            CHECK(p.kind == propanim::TrackValueKind::Quat);
        }
        else if (p.propertyPath.AsView() == StringView(u8"intensity"))
        {
            sawIntensity = true;
            CHECK(p.kind == propanim::TrackValueKind::Float);
        }
        else if (p.propertyPath.AsView() == StringView(u8"light.tint"))
        {
            sawTint = true;
            CHECK(p.kind == propanim::TrackValueKind::Color);
        }
        else if (p.propertyPath.AsView() == StringView(u8"flags"))
        {
            sawFlags = true;
        }
    }
    CHECK(sawPosition);
    CHECK(sawRotation);
    CHECK(sawIntensity);
    CHECK(sawTint);
    CHECK_FALSE(sawFlags); // the i32 leaf is not animatable
}

TEST_CASE("propanim-panel: the panel is a valid clip-editor host (add-track routes through its stack)")
{
    EnsureRegistered();
    scene::Scene sc(DefaultAllocator(), u8"host-test");
    Selection<Guid> selection;
    EditorContext editorCtx{DefaultAllocator()};
    EditorCommandStack stack;
    auto panel = MakePanel(editorCtx, sc, stack, selection);

    CHECK_FALSE(panel->HasClip());

    panel->View().AddTrack(u8"Transform", u8"position", propanim::TrackValueKind::Float3);
    CHECK(panel->Clip().tracks.Size() == 1);
    CHECK(stack.CanUndo());
    stack.Undo();
    CHECK(panel->Clip().tracks.Size() == 0);

    // No selected entity -> add-from-selection is a no-op, never a crash.
    CHECK(panel->AddTracksFromSelection(panel->View()) == 0);
}

namespace
{
    void EnsurePreviewCompRegistered()
    {
        static bool done = false;
        if (done)
        {
            return;
        }
        done = true;
        RegisterCoreTypes();          // reflects Float3 (the property's value type)
        RttiRegisterValue_PreviewComp(); // patches TypeOf<PreviewComp>() with the "position" property
    }

    CurveKey Kv(f32 t, f32 v)
    {
        CurveKey k;
        k.time = t;
        k.value = v;
        return k;
    }

    // A constant Float3 track (single key per channel -> Sample is constant) at `path` on `comp`.
    propanim::PropertyTrack MakeFloat3TrackOn(StringView comp, StringView path, f32 x, f32 y, f32 z)
    {
        propanim::PropertyTrack track;
        track.componentType = String(comp);
        track.propertyPath = String(path);
        track.kind = propanim::TrackValueKind::Float3;
        track.channels[0].AddKey(Kv(0.0f, x));
        track.channels[1].AddKey(Kv(0.0f, y));
        track.channels[2].AddKey(Kv(0.0f, z));
        return track;
    }

    propanim::PropertyTrack MakePositionTrackOn(StringView comp, f32 x, f32 y, f32 z)
    {
        return MakeFloat3TrackOn(comp, u8"position", x, y, z);
    }

    propanim::PropertyTrack MakePositionTrack(f32 x, f32 y, f32 z)
    {
        return MakePositionTrackOn(u8"PreviewComp", x, y, z);
    }
}

TEST_CASE("propanim-panel: preview writes sampled values transiently, restores, never dirties")
{
    EnsurePreviewCompRegistered();

    scene::Scene sc(DefaultAllocator(), u8"preview-test");
    auto* mgr = sc.AddSystem<scene::ComponentManager<PreviewComp>>();
    const scene::EntityHandle e = sc.CreateEntity(u8"e0");
    mgr->Add(e).position = Float3{5.0f, 6.0f, 7.0f}; // the original (pre-preview) value
    const Guid id = sc.GetEntityId(e);

    Selection<Guid> selection;
    selection.Set(id);
    EditorContext editorCtx{DefaultAllocator()};
    EditorCommandStack stack;
    auto panel = MakePanel(editorCtx, sc, stack, selection);
    panel->Clip().tracks.PushBack(MakePositionTrack(100.0f, 200.0f, 300.0f));

    // Scrub -> the sampled values are written onto the live component.
    panel->OnScrubTimeChanged(0.5f);
    CHECK(panel->IsPreviewing());
    REQUIRE(mgr->Get(e) != nullptr);
    CHECK(mgr->Get(e)->position.x == doctest::Approx(100.0f));
    CHECK(mgr->Get(e)->position.y == doctest::Approx(200.0f));
    CHECK(mgr->Get(e)->position.z == doctest::Approx(300.0f));

    // The preview goes NOWHERE near the command stack (nothing to undo) - the document stays clean.
    CHECK_FALSE(stack.CanUndo());

    // Stopping restores the snapshot exactly.
    panel->StopPreview();
    CHECK_FALSE(panel->IsPreviewing());
    CHECK(mgr->Get(e)->position.x == doctest::Approx(5.0f));
    CHECK(mgr->Get(e)->position.y == doctest::Approx(6.0f));
    CHECK(mgr->Get(e)->position.z == doctest::Approx(7.0f));
}

TEST_CASE("propanim-panel: preview is disabled outside EDIT (Simulate/Play locks it)")
{
    EnsurePreviewCompRegistered();

    scene::Scene sc(DefaultAllocator(), u8"preview-sim");
    auto* mgr = sc.AddSystem<scene::ComponentManager<PreviewComp>>();
    const scene::EntityHandle e = sc.CreateEntity(u8"e0");
    mgr->Add(e).position = Float3{1.0f, 1.0f, 1.0f};
    const Guid id = sc.GetEntityId(e);

    Selection<Guid> selection;
    selection.Set(id);
    EditorContext editorCtx{DefaultAllocator()};
    EditorCommandStack stack;
    auto panel = MakePanel(editorCtx, sc, stack, selection);
    panel->Clip().tracks.PushBack(MakePositionTrack(9.0f, 9.0f, 9.0f));

    // Simulate is on (editingLocked): the frame tick stands the preview down.
    panel->Tick(0.0f, true);
    panel->OnScrubTimeChanged(0.5f);
    CHECK_FALSE(panel->IsPreviewing());
    CHECK(mgr->Get(e)->position.x == doctest::Approx(1.0f)); // untouched

    // Back to EDIT: the same scrub now previews.
    panel->Tick(0.0f, false);
    panel->OnScrubTimeChanged(0.5f);
    CHECK(panel->IsPreviewing());
    CHECK(mgr->Get(e)->position.x == doctest::Approx(9.0f));
    panel->StopPreview();
    CHECK(mgr->Get(e)->position.x == doctest::Approx(1.0f));
}

TEST_CASE("propanim-panel: changing the selected entity restores the old one and previews the new")
{
    EnsurePreviewCompRegistered();

    scene::Scene sc(DefaultAllocator(), u8"preview-swap");
    auto* mgr = sc.AddSystem<scene::ComponentManager<PreviewComp>>();
    const scene::EntityHandle a = sc.CreateEntity(u8"a");
    const scene::EntityHandle b = sc.CreateEntity(u8"b");
    mgr->Add(a).position = Float3{1.0f, 0.0f, 0.0f};
    mgr->Add(b).position = Float3{2.0f, 0.0f, 0.0f};

    Selection<Guid> selection;
    selection.Set(sc.GetEntityId(a));
    EditorContext editorCtx{DefaultAllocator()};
    EditorCommandStack stack;
    auto panel = MakePanel(editorCtx, sc, stack, selection);
    panel->Clip().tracks.PushBack(MakePositionTrack(50.0f, 0.0f, 0.0f));

    panel->OnScrubTimeChanged(0.0f); // preview A
    CHECK(mgr->Get(a)->position.x == doctest::Approx(50.0f));

    // Rebind to B and scrub: A is restored (live re-resolve, no cached instance), B is
    // previewed. Selection alone no longer retargets - binding is the explicit act.
    selection.Set(sc.GetEntityId(b));
    panel->BindSelectedEntity();
    panel->OnScrubTimeChanged(0.0f);
    CHECK(mgr->Get(a)->position.x == doctest::Approx(1.0f)); // restored
    CHECK(mgr->Get(b)->position.x == doctest::Approx(50.0f)); // previewed

    panel->StopPreview();
    CHECK(mgr->Get(b)->position.x == doctest::Approx(2.0f)); // restored
}

TEST_CASE("propanim-panel: re-snapshots when the track set changes mid-preview (#6)")
{
    EnsurePreviewCompRegistered();

    scene::Scene sc(DefaultAllocator(), u8"resnapshot");
    const scene::EntityHandle e = sc.CreateEntity(u8"e0");
    sc.SetLocalPosition(e, Float3{5.0f, 6.0f, 7.0f}); // scale defaults to (1,1,1)
    Selection<Guid> selection;
    selection.Set(sc.GetEntityId(e));
    EditorContext editorCtx{DefaultAllocator()};
    EditorCommandStack stack;
    auto panel = MakePanel(editorCtx, sc, stack, selection);

    // Preview with ONE track (Transform.position). Snapshot identity = {Transform|position}.
    panel->Clip().tracks.PushBack(MakePositionTrackOn(u8"Transform", 100.0f, 200.0f, 300.0f));
    panel->OnScrubTimeChanged(0.5f);
    CHECK(sc.GetLocalTransform(e).position.x == doctest::Approx(100.0f));

    // Add a SECOND track (Transform.scale) while previewing - the track identity changed, so the next
    // scrub must re-snapshot (else the scale write below would never be restored: the #6 defect).
    panel->Clip().tracks.PushBack(MakeFloat3TrackOn(u8"Transform", u8"scale", 9.0f, 9.0f, 9.0f));
    panel->OnScrubTimeChanged(0.5f);
    CHECK(sc.GetLocalTransform(e).position.x == doctest::Approx(100.0f)); // still previewed
    CHECK(sc.GetLocalTransform(e).scale.x == doctest::Approx(9.0f));      // new track previewed

    // Stop -> BOTH targets restored (position and the newly-added scale), proving the re-snapshot.
    panel->StopPreview();
    CHECK(sc.GetLocalTransform(e).position.x == doctest::Approx(5.0f));
    CHECK(sc.GetLocalTransform(e).scale.x == doctest::Approx(1.0f)); // would be 9 without the #6 fix
}

TEST_CASE("propanim-panel: add-from-selection seeds the entity Transform (position/rotation/scale)")
{
    EnsurePreviewCompRegistered(); // RegisterCoreTypes reflects Transform

    scene::Scene sc(DefaultAllocator(), u8"seed");
    const scene::EntityHandle e = sc.CreateEntity(u8"e0");
    Selection<Guid> selection;
    selection.Set(sc.GetEntityId(e));
    EditorContext editorCtx{DefaultAllocator()};
    EditorCommandStack stack;
    auto panel = MakePanel(editorCtx, sc, stack, selection);
    const usize added = panel->AddTracksFromSelection(panel->View());
    CHECK(added >= 3); // an entity with no reflected components still animates its Transform

    int pos = 0, rot = 0, scl = 0;
    for (const propanim::PropertyTrack& t : panel->Clip().tracks)
    {
        if (t.componentType.AsView() != StringView(u8"Transform"))
        {
            continue;
        }
        if (t.propertyPath.AsView() == StringView(u8"position"))
        {
            ++pos;
            CHECK(t.kind == propanim::TrackValueKind::Float3);
        }
        else if (t.propertyPath.AsView() == StringView(u8"rotation"))
        {
            ++rot;
            CHECK(t.kind == propanim::TrackValueKind::Quat);
        }
        else if (t.propertyPath.AsView() == StringView(u8"scale"))
        {
            ++scl;
            CHECK(t.kind == propanim::TrackValueKind::Float3);
        }
    }
    CHECK(pos == 1);
    CHECK(rot == 1);
    CHECK(scl == 1);
}

TEST_CASE("propanim-panel: preview drives the entity's built-in Transform and restores it")
{
    EnsurePreviewCompRegistered();

    scene::Scene sc(DefaultAllocator(), u8"tprev");
    const scene::EntityHandle e = sc.CreateEntity(u8"e0");
    sc.SetLocalPosition(e, Float3{5.0f, 6.0f, 7.0f}); // original
    Selection<Guid> selection;
    selection.Set(sc.GetEntityId(e));
    EditorContext editorCtx{DefaultAllocator()};
    EditorCommandStack stack;
    auto panel = MakePanel(editorCtx, sc, stack, selection);
    panel->Clip().tracks.PushBack(MakePositionTrackOn(u8"Transform", 100.0f, 200.0f, 300.0f));

    panel->OnScrubTimeChanged(0.5f);
    CHECK(panel->IsPreviewing());
    CHECK(sc.GetLocalTransform(e).position.x == doctest::Approx(100.0f));
    CHECK(sc.GetLocalTransform(e).position.y == doctest::Approx(200.0f));
    CHECK_FALSE(stack.CanUndo()); // preview never dirties the document

    panel->StopPreview();
    CHECK(sc.GetLocalTransform(e).position.x == doctest::Approx(5.0f)); // restored
    CHECK(sc.GetLocalTransform(e).position.z == doctest::Approx(7.0f));
}

namespace
{
    // A Float3 position track on PreviewComp whose X ramps 0 -> x1 over [0,1] (y,z stay 0). Two keys
    // -> duration 1, non-constant in X (default Linear interp) so the playhead position is observable.
    propanim::PropertyTrack MakeRampTrack(f32 x1)
    {
        propanim::PropertyTrack track;
        track.componentType = String(u8"PreviewComp");
        track.propertyPath = String(u8"position");
        track.kind = propanim::TrackValueKind::Float3;
        track.channels[0].AddKey(Kv(0.0f, 0.0f));
        track.channels[0].AddKey(Kv(1.0f, x1));
        track.channels[1].AddKey(Kv(0.0f, 0.0f));
        track.channels[2].AddKey(Kv(0.0f, 0.0f));
        return track;
    }

    // A panel over a scene with one PreviewComp entity selected + a ramp track loaded.
    struct RampFixture
    {
        scene::Scene sc{DefaultAllocator(), u8"xport"};
        scene::ComponentManager<PreviewComp>* mgr = nullptr;
        scene::EntityHandle e;
        Selection<Guid> selection;
        EditorContext editorCtx{DefaultAllocator()};
        EditorCommandStack stack;
        RefPtr<PropertyAnimationPanel> panel;

        RampFixture()
        {
            mgr = sc.AddSystem<scene::ComponentManager<PreviewComp>>();
            e = sc.CreateEntity(u8"e0");
            mgr->Add(e).position = Float3{0.0f, 0.0f, 0.0f};
            selection.Set(sc.GetEntityId(e));
            panel = MakePanel(editorCtx, sc, stack, selection);
            panel->Clip().tracks.PushBack(MakeRampTrack(10.0f)); // duration 1
        }
        [[nodiscard]] f32 X() const { return mgr->Get(e)->position.x; }
    };
}

TEST_CASE("propanim-panel: transport advances the playhead each Tick and loops by default")
{
    EnsurePreviewCompRegistered();
    RampFixture f;

    CHECK_FALSE(f.panel->IsPlaying());
    f.panel->Play();
    CHECK(f.panel->IsPlaying());

    f.panel->Tick(0.3f, false);
    CHECK(f.panel->PlayheadTime() == doctest::Approx(0.3f));
    f.panel->Tick(0.3f, false);
    CHECK(f.panel->PlayheadTime() == doctest::Approx(0.6f));

    // 0.6 + 0.6 = 1.2 -> wraps to 0.2 (loop defaults on), still playing.
    f.panel->Tick(0.6f, false);
    CHECK(f.panel->PlayheadTime() == doctest::Approx(0.2f));
    CHECK(f.panel->IsPlaying());
}

TEST_CASE("propanim-panel: loop off clamps at the end and stops")
{
    EnsurePreviewCompRegistered();
    RampFixture f;
    f.panel->SetLooping(false);
    CHECK_FALSE(f.panel->IsLooping());

    f.panel->Play();
    f.panel->Tick(2.0f, false); // past the end
    CHECK(f.panel->PlayheadTime() == doctest::Approx(1.0f));
    CHECK_FALSE(f.panel->IsPlaying());
}

TEST_CASE("propanim-panel: stop rewinds the playhead to 0")
{
    EnsurePreviewCompRegistered();
    RampFixture f;
    f.panel->Play();
    f.panel->Tick(0.5f, false);
    CHECK(f.panel->PlayheadTime() == doctest::Approx(0.5f));

    f.panel->Stop();
    CHECK(f.panel->PlayheadTime() == doctest::Approx(0.0f));
    CHECK_FALSE(f.panel->IsPlaying());
}

TEST_CASE("propanim-panel: Simulate stops editor playback (Tick editingLocked)")
{
    EnsurePreviewCompRegistered();
    RampFixture f;
    f.panel->Play();
    f.panel->Tick(0.1f, false);
    CHECK(f.panel->IsPlaying());

    f.panel->Tick(0.1f, true); // Simulate begins
    CHECK_FALSE(f.panel->IsPlaying());
}

TEST_CASE("propanim-panel: playback drives the live preview; scrub is ignored while playing (D6)")
{
    EnsurePreviewCompRegistered();
    RampFixture f;
    f.panel->Play();
    f.panel->Tick(0.5f, false);
    CHECK(f.X() == doctest::Approx(5.0f)); // ramp midpoint previewed by the advance loop

    // A scrub while actively playing is ignored - the transport owns the playhead (D6).
    f.panel->OnScrubTimeChanged(0.9f);
    CHECK(f.panel->PlayheadTime() == doctest::Approx(0.5f)); // unchanged
    CHECK(f.X() == doctest::Approx(5.0f));                   // NOT jumped to 9

    // Stopped -> a scrub now repositions the clock + preview.
    f.panel->Stop();
    f.panel->OnScrubTimeChanged(0.9f);
    CHECK(f.panel->PlayheadTime() == doctest::Approx(0.9f));
    CHECK(f.X() == doctest::Approx(9.0f));
}

TEST_CASE("propanim-panel: dopesheet lane feed + key drag commits a move and re-selects (D4)")
{
    EnsurePreviewCompRegistered();
    scene::Scene sc(DefaultAllocator(), u8"dope");
    Selection<Guid> selection;
    EditorContext editorCtx{DefaultAllocator()};
    EditorCommandStack stack;
    auto panel = MakePanel(editorCtx, sc, stack, selection);

    // A Float3 position track with keys at t=0 and t=1 on each channel.
    propanim::PropertyTrack track;
    track.componentType = String(u8"Transform");
    track.propertyPath = String(u8"position");
    track.kind = propanim::TrackValueKind::Float3;
    for (int c = 0; c < 3; ++c)
    {
        track.channels[c].AddKey(Kv(0.0f, 0.0f));
        track.channels[c].AddKey(Kv(1.0f, 5.0f));
    }
    panel->Clip().tracks.PushBack(Move(track));
    panel->OnClipViewRebuilt(); // rebuild the dopesheet lanes from the clip

    auto& dope = panel->Dopesheet();
    dope.SetPixelsPerSecond(100.0f); // t -> gutter + t*100; lane 0 cy = 24 + 11 = 35
    CHECK(dope.LaneCount() == 1u);
    // The label gutter (the dopesheet IS the track list) offsets the grid: x = 140 + t*100.
    const f32 gutter = dope.LabelColumnWidth;
    CHECK(gutter == doctest::Approx(140.0f));

    // Drag the t=1 marker by +50px = +0.5s.
    foundation::ui::MouseEventArgs down;
    down.Button = foundation::ui::MouseButton::Left;
    down.X = gutter + 100.0f;
    down.Y = 35.0f;
    dope.OnMouseDown(down);
    CHECK(dope.IsKeySelected(0, 1));

    foundation::ui::MouseEventArgs move;
    move.Button = foundation::ui::MouseButton::Left;
    move.X = gutter + 150.0f;
    move.Y = 35.0f;
    dope.OnMouseMove(move);
    foundation::ui::MouseEventArgs up;
    up.Button = foundation::ui::MouseButton::Left;
    up.X = gutter + 150.0f;
    up.Y = 35.0f;
    dope.OnMouseUp(up);

    // Every channel's second key moved 1.0 -> 1.5 as ONE undo step, and the marker re-selected by time.
    REQUIRE(panel->Clip().tracks.Size() == 1u);
    const Array<CurveKey>& ch0 = panel->Clip().tracks[0].channels[0].Keys();
    REQUIRE(ch0.Size() == 2u);
    CHECK(ch0[1].time == doctest::Approx(1.5f));
    CHECK(panel->Clip().tracks[0].channels[2].Keys()[1].time == doctest::Approx(1.5f));
    CHECK(stack.CanUndo());
    CHECK(dope.IsKeySelected(0, 1));

    // Undo restores t=1.
    stack.Undo();
    CHECK(panel->Clip().tracks[0].channels[0].Keys()[1].time == doctest::Approx(1.0f));
}

TEST_CASE("propanim-panel: SetClipDuration authors the clip length, clamped to the last key, undoable")
{
    EnsurePreviewCompRegistered();
    scene::Scene sc(DefaultAllocator(), u8"dur");
    Selection<Guid> selection;
    EditorContext editorCtx{DefaultAllocator()};
    EditorCommandStack stack;
    auto panel = MakePanel(editorCtx, sc, stack, selection);

    // Empty clip (computed 0): author a 5s length.
    panel->SetClipDuration(5.0f);
    CHECK(panel->Clip().duration == doctest::Approx(5.0f));
    CHECK(stack.CanUndo());
    stack.Undo();
    CHECK(panel->Clip().duration == doctest::Approx(0.0f));

    // A track with a key at t=3 (computed 3): authoring 1s clamps UP to 3 (a duration can't cut a key).
    propanim::PropertyTrack track;
    track.componentType = String(u8"Transform");
    track.propertyPath = String(u8"position");
    track.kind = propanim::TrackValueKind::Float3;
    track.channels[0].AddKey(Kv(3.0f, 0.0f));
    panel->Clip().tracks.PushBack(Move(track));
    panel->SetClipDuration(1.0f);
    CHECK(panel->Clip().duration == doctest::Approx(3.0f));
}

TEST_CASE("propanim-panel: exclusive empty state - no clip means no editing surface")
{
    EnsurePreviewCompRegistered();
    scene::Scene sc(DefaultAllocator(), u8"empty-state");
    Selection<Guid> selection;
    EditorContext editorCtx{DefaultAllocator()};
    EditorCommandStack stack;
    auto panel = MakePanel(editorCtx, sc, stack, selection);

    // No clip loaded: the whole editing surface (transport + dopesheet + track editor,
    // all inside the body the dopesheet lives in) is Gone - only the empty-state message
    // + Create/Open remain (workflow 2026-08-17, "no scratch clip").
    CHECK(!panel->HasClip());
    foundation::ui::View* body = panel->Dopesheet().Parent;
    REQUIRE(body != nullptr);
    CHECK(body->Visibility == foundation::ui::Visibility::Gone);
}

TEST_CASE("propanim-panel: ReadSceneValue captures the live Transform + component values")
{
    EnsurePreviewCompRegistered();
    scene::Scene sc(DefaultAllocator(), u8"capture");
    const scene::EntityHandle e = sc.CreateEntity(u8"hero");
    foundation::core::Transform t = sc.GetLocalTransform(e);
    t.position = Float3{4.0f, 5.0f, 6.0f};
    sc.SetLocalTransform(e, t);

    Selection<Guid> selection;
    selection.Set(sc.GetEntityId(e));
    EditorContext editorCtx{DefaultAllocator()};
    EditorCommandStack stack;
    auto panel = MakePanel(editorCtx, sc, stack, selection);

    // The key-from-scene source: the selected entity's LIVE transform.
    const Variant v = panel->ReadSceneValue(u8"Transform", u8"position");
    REQUIRE(!v.IsEmpty());
    REQUIRE(v.Is<Float3>());
    CHECK(v.Get<Float3>().x == doctest::Approx(4.0f));
    CHECK(v.Get<Float3>().z == doctest::Approx(6.0f));

    // Unresolvable paths answer empty (the warn-and-skip contract).
    CHECK(panel->ReadSceneValue(u8"Transform", u8"nope").IsEmpty());
    CHECK(panel->ReadSceneValue(u8"NoSuchComponent", u8"position").IsEmpty());

    // Clearing the SELECTION changes nothing - the binding is session state, not selection.
    selection.Clear();
    CHECK(!panel->ReadSceneValue(u8"Transform", u8"position").IsEmpty());

    // No bound entity -> empty (the workflow's capture gate).
    panel->BindEntity(Guid{});
    CHECK(panel->ReadSceneValue(u8"Transform", u8"position").IsEmpty());
}
