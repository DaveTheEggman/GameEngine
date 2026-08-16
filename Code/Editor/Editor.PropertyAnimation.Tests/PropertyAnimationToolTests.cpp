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
import foundation.propertyanimation;
import editor.core;
import editor.propertyanimation;
import editor.viewporttools;

using namespace foundation::core;
using namespace editor;

namespace propanim = foundation::propertyanimation;

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
