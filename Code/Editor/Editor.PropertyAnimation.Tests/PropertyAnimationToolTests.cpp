// PropertyAnimationTool tests (property-animation.md Phase H3 - the in-scene authoring mode). Covers
// the host-agnostic logic that does not need a live viewport: the reflected-type -> TrackValueKind
// mapping, the animatable-property collector (against a reflected test component), the tool acting as
// a clip-editor host (add-track routes through its command stack), and the provider wiring that is
// the registration count tripwire (one tool provider + one panel provider, correctly keyed). The
// full add-from-selection scene walk is exercised by the build + manual UAT (it needs a live scene).

#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.ui;
import foundation.scene;
import foundation.propertyanimation;
import editor.core;
import editor.propertyanimation;
import editor.viewporttools;

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

REFLECT_VALUE(PreviewComp, "rtti::propanim::tooltest")
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

    // A foreign tool so the panel provider's defensive downcast guard can be exercised.
    class OtherTool final : public IViewportTool
    {
    public:
        [[nodiscard]] StringView Id() const override { return u8"other"; }
        [[nodiscard]] StringView DisplayName() const override { return u8"Other"; }
        bool Update(const ViewportToolInput&) override { return false; }
    };
}

REFLECT_MEMBERS(TestLight, "rtti::propanim::tooltest") { builder.Property<&TestLight::tint>("tint"); }

REFLECT_MEMBERS(TestComp, "rtti::propanim::tooltest")
{
    builder.Property<&TestComp::position>("position")
        .Property<&TestComp::rotation>("rotation")
        .Property<&TestComp::intensity>("intensity")
        .Property<&TestComp::flags>("flags")
        .Nested<&TestComp::light>("light");
}

TEST_CASE("propanim-tool: InferTrackKind maps the four animatable types, rejects others")
{
    CHECK(InferTrackKind(&TypeOf<f32>()).HasValue());
    CHECK(InferTrackKind(&TypeOf<f32>()).Value() == propanim::TrackValueKind::Float);
    CHECK(InferTrackKind(&TypeOf<Float3>()).Value() == propanim::TrackValueKind::Float3);
    CHECK(InferTrackKind(&TypeOf<Color>()).Value() == propanim::TrackValueKind::Color);
    CHECK(InferTrackKind(&TypeOf<Quaternion>()).Value() == propanim::TrackValueKind::Quat);
    CHECK_FALSE(InferTrackKind(&TypeOf<i32>()).HasValue());
    CHECK_FALSE(InferTrackKind(nullptr).HasValue());
}

TEST_CASE("propanim-tool: CollectAnimatableProperties seeds leaves + nested, skips non-animatable")
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

TEST_CASE("propanim-tool: the tool is a valid clip-editor host (add-track routes through its stack)")
{
    EditorContext editorCtx;
    EditorCommandStack stack;
    ViewportToolHostContext ctx;
    ctx.commands = &stack; // scene + selection null: this test does not walk the scene
    PropertyAnimationTool tool(ctx, editorCtx);

    CHECK(tool.Id() == StringView(u8"property.animation"));
    CHECK_FALSE(tool.HasClip());

    ClipEditorView view(tool);
    view.AddTrack(u8"Transform", u8"position", propanim::TrackValueKind::Float3);
    CHECK(tool.Clip().tracks.Size() == 1);
    CHECK(stack.CanUndo());
    stack.Undo();
    CHECK(tool.Clip().tracks.Size() == 0);

    // No scene/selection -> add-from-selection is a no-op, never a crash.
    CHECK(tool.AddTracksFromSelection(view) == 0);
}

TEST_CASE("propanim-tool: provider wiring (registration count tripwire)")
{
    EditorContext editorCtx;

    // Exactly one tool, keyed "property.animation".
    PropertyAnimationToolProvider toolProvider(editorCtx);
    ViewportToolManager manager;
    ViewportToolHostContext ctx;
    toolProvider.CreateTools(manager, ctx);
    CHECK(manager.Count() == 1);
    CHECK(manager.FindById(u8"property.animation") != nullptr);

    // The panel provider is keyed to the same id and builds a real panel for its tool.
    PropertyAnimationPanelProvider panelProvider(editorCtx);
    CHECK(panelProvider.ToolId() == StringView(u8"property.animation"));

    IViewportTool* tool = manager.FindById(u8"property.animation");
    REQUIRE(tool != nullptr);
    RefPtr<foundation::ui::View> panel = panelProvider.CreatePanel(*tool, ctx);
    CHECK(panel.Get() != nullptr);

    // Defensive: a mismatched tool id yields no panel (never a bad downcast).
    OtherTool other;
    CHECK(panelProvider.CreatePanel(other, ctx).Get() == nullptr);
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

    // A constant Float3 track for PreviewComp.position (single key per channel -> Sample is constant).
    propanim::PropertyTrack MakePositionTrack(f32 x, f32 y, f32 z)
    {
        propanim::PropertyTrack track;
        track.componentType = String(u8"PreviewComp");
        track.propertyPath = String(u8"position");
        track.kind = propanim::TrackValueKind::Float3;
        track.channels[0].AddKey(Kv(0.0f, x));
        track.channels[1].AddKey(Kv(0.0f, y));
        track.channels[2].AddKey(Kv(0.0f, z));
        return track;
    }
}

TEST_CASE("propanim-tool: preview writes sampled values transiently, restores, never dirties")
{
    EnsurePreviewCompRegistered();

    scene::Scene sc(u8"preview-test");
    auto* mgr = sc.AddSystem<scene::ComponentManager<PreviewComp>>();
    const scene::EntityHandle e = sc.CreateEntity(u8"e0");
    mgr->Add(e).position = Float3{5.0f, 6.0f, 7.0f}; // the original (pre-preview) value
    const Guid id = sc.GetEntityId(e);

    Selection<Guid> selection;
    selection.Set(id);
    EditorContext editorCtx;
    EditorCommandStack stack;
    ViewportToolHostContext ctx;
    ctx.scene = &sc;
    ctx.commands = &stack;
    ctx.entitySelection = &selection;

    PropertyAnimationTool tool(ctx, editorCtx);
    tool.Clip().tracks.PushBack(MakePositionTrack(100.0f, 200.0f, 300.0f));

    // Scrub -> the sampled values are written onto the live component.
    tool.OnScrubTimeChanged(0.5f);
    CHECK(tool.IsPreviewing());
    REQUIRE(mgr->Get(e) != nullptr);
    CHECK(mgr->Get(e)->position.x == doctest::Approx(100.0f));
    CHECK(mgr->Get(e)->position.y == doctest::Approx(200.0f));
    CHECK(mgr->Get(e)->position.z == doctest::Approx(300.0f));

    // The preview goes NOWHERE near the command stack (nothing to undo) - the document stays clean.
    CHECK_FALSE(stack.CanUndo());

    // Stopping restores the snapshot exactly.
    tool.StopPreview();
    CHECK_FALSE(tool.IsPreviewing());
    CHECK(mgr->Get(e)->position.x == doctest::Approx(5.0f));
    CHECK(mgr->Get(e)->position.y == doctest::Approx(6.0f));
    CHECK(mgr->Get(e)->position.z == doctest::Approx(7.0f));
}

TEST_CASE("propanim-tool: preview is disabled outside EDIT (Simulate/Play locks it)")
{
    EnsurePreviewCompRegistered();

    scene::Scene sc(u8"preview-sim");
    auto* mgr = sc.AddSystem<scene::ComponentManager<PreviewComp>>();
    const scene::EntityHandle e = sc.CreateEntity(u8"e0");
    mgr->Add(e).position = Float3{1.0f, 1.0f, 1.0f};
    const Guid id = sc.GetEntityId(e);

    Selection<Guid> selection;
    selection.Set(id);
    EditorContext editorCtx;
    EditorCommandStack stack;
    ViewportToolHostContext ctx;
    ctx.scene = &sc;
    ctx.commands = &stack;
    ctx.entitySelection = &selection;

    PropertyAnimationTool tool(ctx, editorCtx);
    tool.Clip().tracks.PushBack(MakePositionTrack(9.0f, 9.0f, 9.0f));

    // Simulate is on (editingLocked): the frame update stands the preview down.
    ViewportToolInput simInput;
    simInput.editingLocked = true;
    tool.Update(simInput);
    tool.OnScrubTimeChanged(0.5f);
    CHECK_FALSE(tool.IsPreviewing());
    CHECK(mgr->Get(e)->position.x == doctest::Approx(1.0f)); // untouched

    // Back to EDIT: the same scrub now previews.
    ViewportToolInput editInput;
    editInput.editingLocked = false;
    tool.Update(editInput);
    tool.OnScrubTimeChanged(0.5f);
    CHECK(tool.IsPreviewing());
    CHECK(mgr->Get(e)->position.x == doctest::Approx(9.0f));
    tool.StopPreview();
    CHECK(mgr->Get(e)->position.x == doctest::Approx(1.0f));
}

TEST_CASE("propanim-tool: changing the selected entity restores the old one and previews the new")
{
    EnsurePreviewCompRegistered();

    scene::Scene sc(u8"preview-swap");
    auto* mgr = sc.AddSystem<scene::ComponentManager<PreviewComp>>();
    const scene::EntityHandle a = sc.CreateEntity(u8"a");
    const scene::EntityHandle b = sc.CreateEntity(u8"b");
    mgr->Add(a).position = Float3{1.0f, 0.0f, 0.0f};
    mgr->Add(b).position = Float3{2.0f, 0.0f, 0.0f};

    Selection<Guid> selection;
    selection.Set(sc.GetEntityId(a));
    EditorContext editorCtx;
    EditorCommandStack stack;
    ViewportToolHostContext ctx;
    ctx.scene = &sc;
    ctx.commands = &stack;
    ctx.entitySelection = &selection;

    PropertyAnimationTool tool(ctx, editorCtx);
    tool.Clip().tracks.PushBack(MakePositionTrack(50.0f, 0.0f, 0.0f));

    tool.OnScrubTimeChanged(0.0f); // preview A
    CHECK(mgr->Get(a)->position.x == doctest::Approx(50.0f));

    // Select B and scrub: A is restored (live re-resolve, no cached instance), B is previewed.
    selection.Set(sc.GetEntityId(b));
    tool.OnScrubTimeChanged(0.0f);
    CHECK(mgr->Get(a)->position.x == doctest::Approx(1.0f)); // restored
    CHECK(mgr->Get(b)->position.x == doctest::Approx(50.0f)); // previewed

    tool.StopPreview();
    CHECK(mgr->Get(b)->position.x == doctest::Approx(2.0f)); // restored
}
