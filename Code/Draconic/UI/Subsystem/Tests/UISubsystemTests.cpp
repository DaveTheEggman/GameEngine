// Headless UISubsystem: canvas instantiation from documents (direct Ref override, no db),
// hot-reload rebuild, visibility/interactivity sync, serialization round-trip. No GPU -
// RenderOverlay untested here (the sample + editor smoke cover it on-screen).
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.runtime;
import draconic.scene;
import draconic.scene.subsystem;
import draconic.ui;
import draconic.ui.resource;
import draconic.ui.subsystem;
import draconic.render.api;
import draconic.render.subsystem;
import draconic.shell;
import draconic.input;
import draconic.input.subsystem;

using namespace draconic::core;
using namespace draconic::ui;
namespace dscene = draconic::scene;
namespace rt = draconic::runtime;

namespace
{
    RefPtr<UIDocument> MakeDocument(StringView markup)
    {
        RefPtr<UIDocument> document = MakeRef<UIDocument>(DefaultAllocator());
        document->markup = String(markup);
        return document;
    }
}

TEST_CASE("ui.subsystem: canvases instantiate, hot-reload, and sync visibility")
{
    rt::Context ctx;
    auto* scenes = ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();

    dscene::Scene* scene = scenes->CreateScene(u8"menu");
    auto* canvases = scene->GetSystem<UICanvasComponentManager>();
    REQUIRE(canvases != nullptr);   // injected by the subsystem (ISceneAware)

    dscene::EntityHandle e = scene->CreateEntity(u8"pause");
    UICanvasComponent& canvas = canvases->Add(e);
    RefPtr<UIDocument> document = MakeDocument(
        u8"<FlexLayout><Label id=\"title\" text=\"Paused\" /><Button id=\"resume-btn\" text=\"Resume\" /></FlexLayout>");
    canvas.document = document;   // Ref direct override (no content db)

    ctx.BeginFrame(1.0f / 60.0f);   // subsystem builds the tree

    REQUIRE(canvas.root.Get() != nullptr);
    auto* group = Cast<ViewGroup>(canvas.root.Get());
    REQUIRE(group != nullptr);
    CHECK(group->FindByName(u8"resume-btn") != nullptr);
    // The tier split: the canvas parents into ITS SCENE's root (above that scene's
    // billboard layer); the screen root holds only the global overlay layer.
    RootView* sceneRoot = ui->SceneRoot(*scene);
    REQUIRE(sceneRoot != nullptr);
    CHECK(sceneRoot->ChildCount() == 2u);          // billboard layer + canvas
    CHECK(ui->ScreenRoot()->ChildCount() == 1u);   // overlay layer only
    CHECK(Cast<ViewGroup>(sceneRoot)->FindByName(u8"resume-btn") != nullptr);

    // Hot reload: a NEW document product rebuilds the tree (structure proves it - a
    // pointer compare can false-negative on allocator address reuse).
    canvas.document = MakeDocument(u8"<FlexLayout><Label id=\"only\" text=\"v2\" /></FlexLayout>");
    ctx.BeginFrame(1.0f / 60.0f);
    REQUIRE(canvas.root.Get() != nullptr);
    CHECK(Cast<ViewGroup>(canvas.root.Get())->FindByName(u8"only") != nullptr);
    CHECK(Cast<ViewGroup>(canvas.root.Get())->FindByName(u8"resume-btn") == nullptr);

    // Visibility/interactivity flow into the live tree each frame.
    canvas.visible = false;
    canvas.interactive = false;
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(canvas.root->Visibility == VisibilityValue::Gone);
    CHECK_FALSE(canvas.root->IsHitTestVisible);

    // Scene teardown detaches cleanly.
    scenes->DestroyScene(scene);
    ctx.BeginFrame(1.0f / 60.0f);
    ctx.Shutdown();
}

TEST_CASE("ui.subsystem: canvas component serialization round-trips")
{
    UICanvasComponent a;
    a.document.SetId(Guid{ 1, 2 });
    a.theme.SetId(Guid{ 3, 4 });
    a.order = 7;
    a.visible = false;
    a.interactive = false;
    a.scalerMode = CanvasScalerMode::ReferenceResolution;
    a.referenceResolution = Float2{ 1280.0f, 800.0f };

    MemoryStream buffer;
    {
        BinarySerializer writer(buffer, SerializeMode::Write);
        Serialize(writer, a);
    }
    (void)buffer.Seek(0, SeekOrigin::Begin);
    UICanvasComponent b;
    {
        BinarySerializer reader(buffer, SerializeMode::Read);
        Serialize(reader, b);
    }
    CHECK(b.document.id == a.document.id);
    CHECK(b.theme.id == a.theme.id);
    CHECK(b.order == 7);
    CHECK_FALSE(b.visible);
    CHECK_FALSE(b.interactive);
    CHECK(b.scalerMode == CanvasScalerMode::ReferenceResolution);
    CHECK(b.referenceResolution.x == doctest::Approx(1280.0f));
}

TEST_CASE("ui.subsystem: billboards project through the scene camera and park behind it")
{
    rt::Context ctx;
    auto* scenes = ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    (void)ui;
    // The camera manager comes from the render subsystem normally; add it directly here.
    ctx.Startup();
    dscene::Scene* scene = scenes->CreateScene(u8"world");
    scene->AddSystem<draconic::render::CameraComponentManager>();

    // A camera at origin looking down -Z (identity rotation), and two anchors.
    dscene::EntityHandle cam = scene->CreateEntity(u8"cam");
    scene->GetSystem<draconic::render::CameraComponentManager>()->Add(cam);
    dscene::EntityHandle front = scene->CreateEntity(u8"front");
    scene->SetLocalPosition(front, Float3{ 0.0f, 0.0f, -10.0f });
    dscene::EntityHandle behind = scene->CreateEntity(u8"behind");
    scene->SetLocalPosition(behind, Float3{ 0.0f, 0.0f, 10.0f });
    scene->UpdateTransforms();

    auto* billboards = scene->GetSystem<UIBillboardComponentManager>();
    REQUIRE(billboards != nullptr);
    UIBillboardComponent& a = billboards->Add(front);
    a.document = MakeDocument(u8"<Label id=\"name-a\" text=\"A\"/>");
    UIBillboardComponent& b = billboards->Add(behind);
    b.document = MakeDocument(u8"<Label id=\"name-b\" text=\"B\"/>");

    ctx.BeginFrame(1.0f / 60.0f);   // instantiate
    REQUIRE(a.root.Get() != nullptr);
    REQUIRE(b.root.Get() != nullptr);

    // The overlay-role split made the per-view sync directly testable: drive it with a
    // synthetic view whose VP has clip.w = -z_view (camera at origin looking down -Z),
    // the shape every real perspective produces. front (z=-10) is on-axis -> centered;
    // behind (z=+10) gets clip.w < 0 -> parks off-screen.
    draconic::render::SceneOverlayView view;
    view.sceneKey = scene;
    view.viewProjection = Float4x4::Identity();
    view.viewProjection(2, 3) = -1.0f;   // clip.w = -z (row-vector convention)
    view.viewProjection(3, 3) = 0.0f;
    view.targetWidth = 800;
    view.targetHeight = 600;
    view.viewportWidth = 800;
    view.viewportHeight = 600;
    ui->UpdateSceneView(*scene, view);

    auto* lpFront = Cast<AbsoluteLayoutParams>(a.root->LayoutParams.Get());
    auto* lpBehind = Cast<AbsoluteLayoutParams>(b.root->LayoutParams.Get());
    REQUIRE(lpFront != nullptr);
    REQUIRE(lpBehind != nullptr);
    CHECK(lpFront->X == doctest::Approx(400.0f));   // on-axis -> target center
    CHECK(lpFront->Y == doctest::Approx(300.0f));
    CHECK(lpBehind->X == doctest::Approx(-10000.0f));   // behind the camera -> parked
    CHECK(lpBehind->Y == doctest::Approx(-10000.0f));

    // Sub-rect view (split-screen half): billboards land in VIEWPORT pixels - the VG
    // viewport seam places the whole root at the view's rect, so the on-axis anchor
    // centers within the HALF, not the full target.
    view.viewportX = 400;
    view.viewportWidth = 400;
    view.viewportHeight = 300;
    ui->UpdateSceneView(*scene, view);
    CHECK(lpFront->X == doctest::Approx(200.0f));   // center of the 400x300 half
    CHECK(lpFront->Y == doctest::Approx(150.0f));

    // Scene isolation is structural now: another scene's canvas parents into ITS root.
    dscene::Scene* other = scenes->CreateScene(u8"other");
    auto* otherCanvases = other->GetSystem<UICanvasComponentManager>();
    dscene::EntityHandle e = other->CreateEntity(u8"hud");
    UICanvasComponent& canvas = otherCanvases->Add(e);
    canvas.document = MakeDocument(u8"<Label id=\"x\" text=\"other\"/>");
    ctx.BeginFrame(1.0f / 60.0f);
    REQUIRE(canvas.root.Get() != nullptr);
    CHECK(Cast<ViewGroup>(ui->SceneRoot(*other))->FindByName(u8"x") != nullptr);
    CHECK(Cast<ViewGroup>(ui->SceneRoot(*scene))->FindByName(u8"x") == nullptr);

    ctx.Shutdown();
}

TEST_CASE("ui.subsystem: the scene-less screen tier survives scene swaps and stays topmost")
{
    rt::Context ctx;
    auto* scenes = ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();

    dscene::Scene* scene = scenes->CreateScene(u8"level");
    auto* canvases = scene->GetSystem<UICanvasComponentManager>();
    dscene::EntityHandle e = scene->CreateEntity(u8"hud");
    UICanvasComponent& canvas = canvases->Add(e);
    canvas.document = MakeDocument(u8"<Label id=\"hud\" text=\"HUD\"/>");

    RefPtr<UIDocument> loading = MakeRef<UIDocument>(DefaultAllocator());
    loading->markup = String(u8"<Panel><Label id=\"loading\" text=\"Loading...\"/></Panel>");
    RefPtr<View> overlay = ui->PushScreenOverlay(*loading);
    REQUIRE(overlay.Get() != nullptr);
    CHECK(ui->ScreenOverlayCount() == 1);

    ctx.BeginFrame(1.0f / 60.0f);

    // The tier split keeps the screen root scene-free: the canvas lives in ITS scene's
    // root; the overlay rides the screen root (drawn per window target, above scenes).
    CHECK(Cast<ViewGroup>(ui->ScreenRoot())->FindByName(u8"loading") != nullptr);
    CHECK(Cast<ViewGroup>(ui->ScreenRoot())->FindByName(u8"hud") == nullptr);
    CHECK(Cast<ViewGroup>(ui->SceneRoot(*scene))->FindByName(u8"hud") != nullptr);

    // Destroying the scene kills its root+canvas - the GLOBAL overlay survives.
    scenes->DestroyScene(scene);
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(ui->ScreenOverlayCount() == 1);
    CHECK(Cast<ViewGroup>(ui->ScreenRoot())->FindByName(u8"loading") != nullptr);

    ui->RemoveScreenOverlay(overlay.Get());
    CHECK(ui->ScreenOverlayCount() == 0);
    ctx.Shutdown();
}

TEST_CASE("ui.subsystem: an EMPTY overlay layer never blocks canvas hit-testing")
{
    rt::Context ctx;
    auto* scenes = ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();
    dscene::Scene* scene = scenes->CreateScene(u8"level");
    dscene::EntityHandle e = scene->CreateEntity(u8"hud");
    UICanvasComponent& canvas = scene->GetSystem<UICanvasComponentManager>()->Add(e);
    canvas.document = MakeDocument(
        u8"<Flex direction=\"vertical\"><Button id=\"btn\" text=\"hit me\" width=\"200\" height=\"40\"/></Flex>");
    ctx.BeginFrame(1.0f / 60.0f);
    REQUIRE(canvas.root.Get() != nullptr);

    // Lay out both tiers at a known size: the scene root holds the button; the EMPTY
    // screen tier must not intercept anything above it.
    RootView* sceneRoot = ui->SceneRoot(*scene);
    RootView* screenRoot = ui->ScreenRoot();
    REQUIRE(sceneRoot != nullptr);
    sceneRoot->ViewportSize = Float2{ 800.0f, 600.0f };
    screenRoot->ViewportSize = Float2{ 800.0f, 600.0f };
    ui->Context().UpdateRootView(sceneRoot);
    ui->Context().UpdateRootView(screenRoot);
    View* screenHit = screenRoot->HitTest(Float2{ 20.0f, 20.0f });
    CHECK((screenHit == nullptr || screenHit == screenRoot));   // empty tier: transparent
    View* hit = sceneRoot->HitTest(Float2{ 20.0f, 20.0f });
    REQUIRE(hit != nullptr);
    CHECK(hit->Name.AsView() == u8"btn");

    // With a pushed overlay the screen tier DOES block (a modal loading screen must).
    RefPtr<UIDocument> loading = MakeRef<UIDocument>(DefaultAllocator());
    loading->markup = String(u8"<Panel width=\"800\" height=\"600\"><Label text=\"Loading\"/></Panel>");
    RefPtr<View> overlay = ui->PushScreenOverlay(*loading);
    REQUIRE(overlay.Get() != nullptr);
    ctx.BeginFrame(1.0f / 60.0f);
    ui->Context().UpdateRootView(screenRoot);
    View* blocked = screenRoot->HitTest(Float2{ 20.0f, 20.0f });
    REQUIRE(blocked != nullptr);
    CHECK(blocked != screenRoot);   // the occupied overlay layer eats the point

    ctx.Shutdown();
}

namespace
{
    // Minimal pad/provider fakes for the nav pump (the input tests' pattern).
    struct NavFakePad final : draconic::shell::IGamepad
    {
        bool down[static_cast<u32>(draconic::shell::GamepadButton::Count)] = {};
        bool pressed[static_cast<u32>(draconic::shell::GamepadButton::Count)] = {};
        f32 axes[static_cast<u32>(draconic::shell::GamepadAxis::Count)] = {};
        [[nodiscard]] i32 Index() const override { return 0; }
        [[nodiscard]] StringView Name() const override { return u8"fake"; }
        [[nodiscard]] bool Connected() const override { return true; }
        [[nodiscard]] bool IsButtonDown(draconic::shell::GamepadButton b) const override
        { return down[static_cast<u32>(b)]; }
        [[nodiscard]] bool IsButtonPressed(draconic::shell::GamepadButton b) const override
        { return pressed[static_cast<u32>(b)]; }
        [[nodiscard]] bool IsButtonReleased(draconic::shell::GamepadButton) const override { return false; }
        [[nodiscard]] f32 Axis(draconic::shell::GamepadAxis a) const override
        { return axes[static_cast<u32>(a)]; }
        void SetRumble(f32, f32, u32) override {}
    };

    struct NavFakeDevices final : draconic::input::IInputSourceProvider
    {
        NavFakePad pad;
        [[nodiscard]] draconic::shell::IMouse* Mouse() override { return nullptr; }
        [[nodiscard]] draconic::shell::IKeyboard* Keyboard() override { return nullptr; }
        [[nodiscard]] i32 GamepadCount() const override { return 1; }
        [[nodiscard]] draconic::shell::IGamepad* Gamepad(i32 index) override
        { return index == 0 ? &pad : nullptr; }
    };
}

TEST_CASE("ui.subsystem: gamepad dpad moves focus with hold-repeat; South activates")
{
    rt::Context ctx;
    auto* scenes = ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* input = ctx.AddSubsystem<draconic::input::InputSubsystem>(nullptr);
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();

    NavFakeDevices devices;
    input->SetSourceProvider(&devices);

    dscene::Scene* scene = scenes->CreateScene(u8"menu");
    dscene::EntityHandle e = scene->CreateEntity(u8"pause");
    UICanvasComponent& canvas = scene->GetSystem<UICanvasComponentManager>()->Add(e);
    canvas.document = MakeDocument(
        u8"<Flex direction=\"vertical\" spacing=\"4\">"
        u8"<Button id=\"top\" text=\"Top\" width=\"200\" height=\"36\"/>"
        u8"<Button id=\"bottom\" text=\"Bottom\" width=\"200\" height=\"36\"/>"
        u8"</Flex>");
    ctx.BeginFrame(1.0f / 60.0f);
    REQUIRE(canvas.root.Get() != nullptr);
    // The menu lives in the SCENE root; with no pointer and no overlay, the pump routes
    // input there (pad-only nav must reach a pause menu no pointer ever hovered).
    RootView* root = ui->SceneRoot(*scene);
    REQUIRE(root != nullptr);
    root->ViewportSize = Float2{ 800.0f, 600.0f };
    ui->Context().UpdateRootView(root);
    CHECK(ui->Context().ActiveInputRoot() == root);

    FocusManager* focus = ui->Context().GetFocusManager();
    REQUIRE(focus != nullptr);
    CHECK(focus->FocusedView() == nullptr);

    // First Down press bootstraps focus to the first focusable...
    devices.pad.down[static_cast<u32>(draconic::shell::GamepadButton::DPadDown)] = true;
    ctx.BeginFrame(1.0f / 60.0f);
    ui->Context().UpdateRootView(root);
    REQUIRE(focus->FocusedView() != nullptr);
    CHECK(focus->FocusedView()->Name.AsView() == u8"top");

    // ...held: no move until the initial repeat delay elapses, then it advances.
    ctx.BeginFrame(0.1f);
    CHECK(focus->FocusedView()->Name.AsView() == u8"top");
    ctx.BeginFrame(0.35f);   // crosses the 0.4s initial delay
    CHECK(focus->FocusedView()->Name.AsView() == u8"bottom");
    devices.pad.down[static_cast<u32>(draconic::shell::GamepadButton::DPadDown)] = false;
    ctx.BeginFrame(1.0f / 60.0f);

    // South = Submit: the focused button activates through the Return path.
    bool clicked = false;
    Cast<ViewGroup>(canvas.root.Get())->FindByName<Button>(u8"bottom")->OnClick.Add(
        [&clicked](ButtonBase*) { clicked = true; });
    devices.pad.pressed[static_cast<u32>(draconic::shell::GamepadButton::South)] = true;
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(clicked);
    devices.pad.pressed[static_cast<u32>(draconic::shell::GamepadButton::South)] = false;

    // An OCCUPIED screen tier is modal: input routing flips to the screen root.
    RefPtr<UIDocument> modal = MakeRef<UIDocument>(DefaultAllocator());
    modal->markup = String(u8"<Panel><Label text=\"Loading\"/></Panel>");
    RefPtr<View> overlay = ui->PushScreenOverlay(*modal);
    REQUIRE(overlay.Get() != nullptr);
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(ui->Context().ActiveInputRoot() == ui->ScreenRoot());
    ui->RemoveScreenOverlay(overlay.Get());
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(ui->Context().ActiveInputRoot() == root);   // back to the scene tier

    ctx.Shutdown();
}

TEST_CASE("ui.subsystem: preview roots live in the context but never on the screen root")
{
    rt::Context ctx;
    ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();

    // Parse failure -> null (the page keeps its last good preview).
    UIDocument bad;
    bad.markup = String(u8"<NoSuchControl>");
    CHECK(ui->CreatePreview(bad).Get() == nullptr);

    UIDocument good;
    good.markup = String(u8"<FlexLayout><Label id=\"pv\" text=\"preview\" /></FlexLayout>");
    RefPtr<RootView> preview = ui->CreatePreview(good);
    REQUIRE(preview.Get() != nullptr);

    // The document instantiated under the preview root...
    REQUIRE(preview->ChildCount() == 1u);
    CHECK(Cast<ViewGroup>(preview.Get())->FindByName(u8"pv") != nullptr);
    // ...which is NOT parented to the screen root (RenderOverlay draws only the screen
    // root, so a preview can never leak into game targets)...
    const u32 screenChildren = ui->ScreenRoot()->ChildCount();
    CHECK(Cast<ViewGroup>(ui->ScreenRoot())->FindByName(u8"pv") == nullptr);
    // ...and frames tick without disturbing the screen tier.
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(ui->ScreenRoot()->ChildCount() == screenChildren);

    ui->DestroyPreview(preview.Get());
    ctx.BeginFrame(1.0f / 60.0f);
    ctx.Shutdown();
}
