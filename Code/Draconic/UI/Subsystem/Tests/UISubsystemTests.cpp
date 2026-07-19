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
import draconic.shell.null;
import draconic.rhi;
import draconic.rhi.null;
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
    RegisterUIComponentReflection();   // versioned payloads read the type's data version
    UICanvasComponent a;
    a.document.SetId(Guid{ 1, 2 });
    a.theme.SetId(Guid{ 3, 4 });
    a.order = 7;
    a.visible = false;
    a.interactive = false;
    a.scalerMode = CanvasScalerMode::ReferenceResolution;
    a.referenceResolution = Float2{ 1280.0f, 800.0f };
    a.renderMode = CanvasRenderMode::RenderTexture;
    a.renderTextureWidth = 640;
    a.renderTextureHeight = 360;

    MemoryStream buffer;
    {
        BinarySerializer writer(buffer, SerializeMode::Write);
        BeginVersionedPayload(writer, TypeOf<UICanvasComponent>());
        Serialize(writer, a);
        EndVersionedPayload(writer);
    }
    (void)buffer.Seek(0, SeekOrigin::Begin);
    UICanvasComponent b;
    {
        BinarySerializer reader(buffer, SerializeMode::Read);
        BeginVersionedPayload(reader, TypeOf<UICanvasComponent>());
        Serialize(reader, b);
        EndVersionedPayload(reader);
    }
    CHECK(b.document.id == a.document.id);
    CHECK(b.theme.id == a.theme.id);
    CHECK(b.order == 7);
    CHECK_FALSE(b.visible);
    CHECK_FALSE(b.interactive);
    CHECK(b.scalerMode == CanvasScalerMode::ReferenceResolution);
    CHECK(b.referenceResolution.x == doctest::Approx(1280.0f));
    CHECK(b.renderMode == CanvasRenderMode::RenderTexture);   // v2 fields
    CHECK(b.renderTextureWidth == 640u);
    CHECK(b.renderTextureHeight == 360u);
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

TEST_CASE("ui.subsystem: canvases stack by order; billboard layer stays below; despawn sweeps")
{
    rt::Context ctx;
    auto* scenes = ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();

    dscene::Scene* scene = scenes->CreateScene(u8"hud");
    auto* canvases = scene->GetSystem<UICanvasComponentManager>();

    // Components added in order a (order 5), b (order 0), c (order 5 - a TIE with a).
    // Component references are transient (the pool compacts) - set fields right after
    // each Add and re-resolve later via Get.
    dscene::EntityHandle ea = scene->CreateEntity(u8"a");
    {
        UICanvasComponent& a = canvases->Add(ea);
        a.document = MakeDocument(u8"<Label id=\"canvas-a\" text=\"a\"/>");
        a.order = 5;
    }
    dscene::EntityHandle eb = scene->CreateEntity(u8"b");
    {
        UICanvasComponent& b = canvases->Add(eb);
        b.document = MakeDocument(u8"<Label id=\"canvas-b\" text=\"b\"/>");
        b.order = 0;
    }
    dscene::EntityHandle ec = scene->CreateEntity(u8"c");
    {
        UICanvasComponent& c = canvases->Add(ec);
        c.document = MakeDocument(u8"<Label id=\"canvas-c\" text=\"c\"/>");
        c.order = 5;
    }

    ctx.BeginFrame(1.0f / 60.0f);
    RootView* root = ui->SceneRoot(*scene);
    REQUIRE(root != nullptr);
    REQUIRE(root->ChildCount() == 4u);   // billboard layer + 3 canvas hosts

    // Which canvas lives at which child index (child order = draw order, later = on top)?
    auto canvasAt = [root](usize index) -> StringView {
        auto* group = Cast<ViewGroup>(root->GetChildAt(index));
        if (group == nullptr) { return u8""; }
        if (group->FindByName(u8"canvas-a") != nullptr) { return u8"a"; }
        if (group->FindByName(u8"canvas-b") != nullptr) { return u8"b"; }
        if (group->FindByName(u8"canvas-c") != nullptr) { return u8"c"; }
        return u8"";
    };
    // The billboard layer is child 0 (below every canvas) and holds no canvas.
    CHECK(canvasAt(0) == u8"");
    // Sorted by order, STABLE for the a/c tie (component order): b(0), a(5), c(5).
    CHECK(canvasAt(1) == u8"b");
    CHECK(canvasAt(2) == u8"a");
    CHECK(canvasAt(3) == u8"c");

    // An order change re-sorts on the next sync: push b on top.
    canvases->Get(eb)->order = 10;
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(canvasAt(1) == u8"a");
    CHECK(canvasAt(2) == u8"c");
    CHECK(canvasAt(3) == u8"b");

    // Despawning the entity sweeps its host out of the scene root (menus close on
    // despawn even though component managers have no destroy hook).
    scene->DestroyEntity(ec);
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(root->ChildCount() == 3u);
    CHECK(Cast<ViewGroup>(root)->FindByName(u8"canvas-c") == nullptr);
    CHECK(canvasAt(1) == u8"a");
    CHECK(canvasAt(2) == u8"b");

    ctx.Shutdown();
}

TEST_CASE("ui.subsystem: ReferenceResolution scaler lays out at the reference size and scales to fit")
{
    rt::Context ctx;
    auto* scenes = ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();
    dscene::Scene* scene = scenes->CreateScene(u8"menu");
    auto* canvases = scene->GetSystem<UICanvasComponentManager>();
    dscene::EntityHandle e = scene->CreateEntity(u8"hud");
    {
        UICanvasComponent& c = canvases->Add(e);
        c.document = MakeDocument(
            u8"<Flex direction=\"vertical\"><Button id=\"btn\" text=\"go\" width=\"200\" height=\"40\"/></Flex>");
        c.scalerMode = CanvasScalerMode::ReferenceResolution;
        c.referenceResolution = Float2{ 1600.0f, 900.0f };
    }
    ctx.BeginFrame(1.0f / 60.0f);
    UICanvasComponent* c = canvases->Get(e);
    REQUIRE(c != nullptr);
    REQUIRE(c->root.Get() != nullptr);

    // Lay the scene root out at a smaller, differently-proportioned viewport.
    RootView* root = ui->SceneRoot(*scene);
    REQUIRE(root != nullptr);
    root->ViewportSize = Float2{ 800.0f, 600.0f };
    ui->Context().UpdateRootView(root);

    // The document laid out at the REFERENCE size, uniformly scaled by
    // min(800/1600, 600/900) = 0.5 and centered (letterbox: 75px top/bottom).
    CHECK(c->root->Width() == doctest::Approx(1600.0f));
    CHECK(c->root->Height() == doctest::Approx(900.0f));
    CHECK(c->root->Transform.Scale.x == doctest::Approx(0.5f));
    CHECK(c->root->Transform.Scale.y == doctest::Approx(0.5f));
    CHECK(c->root->Bounds.x == doctest::Approx(0.0f));
    CHECK(c->root->Bounds.y == doctest::Approx(75.0f));

    // Hit-testing follows the transform: reference-space (100, 20) draws at
    // (50, 75 + 10) - the button is hit there, and the letterbox bar is empty.
    View* hit = root->HitTest(Float2{ 50.0f, 85.0f });
    REQUIRE(hit != nullptr);
    CHECK(hit->Name.AsView() == u8"btn");
    View* bar = root->HitTest(Float2{ 50.0f, 30.0f });
    CHECK((bar == nullptr || bar == root));

    // Switching back to ConstantPixel restores 1:1 layout on the next sync.
    c->scalerMode = CanvasScalerMode::ConstantPixel;
    ctx.BeginFrame(1.0f / 60.0f);
    ui->Context().UpdateRootView(root);
    c = canvases->Get(e);
    CHECK(c->root->Width() == doctest::Approx(800.0f));
    CHECK(c->root->Transform.Scale.x == doctest::Approx(1.0f));
    View* direct = root->HitTest(Float2{ 100.0f, 20.0f });
    REQUIRE(direct != nullptr);
    CHECK(direct->Name.AsView() == u8"btn");

    ctx.Shutdown();
}

namespace
{
    // An event-first provider fake: no polled devices, just this frame's tagged stream
    // (the shape ShellInputSource/GameViewportInputSource produce for key/text).
    struct EventFakeDevices final : draconic::input::IInputSourceProvider
    {
        Array<draconic::shell::InputEvent> events;
        [[nodiscard]] draconic::shell::IMouse* Mouse() override { return nullptr; }
        [[nodiscard]] draconic::shell::IKeyboard* Keyboard() override { return nullptr; }
        [[nodiscard]] i32 GamepadCount() const override { return 0; }
        [[nodiscard]] draconic::shell::IGamepad* Gamepad(i32) override { return nullptr; }
        [[nodiscard]] Span<const draconic::shell::InputEvent> Events() override
        {
            return { events.Data(), events.Size() };
        }

        void PushKey(draconic::shell::InputEventKind kind, draconic::shell::KeyCode key)
        {
            draconic::shell::InputEvent e;
            e.kind = kind;
            e.key = key;
            events.PushBack(e);
        }
        void PushText(StringView text)
        {
            draconic::shell::InputEvent e;
            e.kind = draconic::shell::InputEventKind::TextInput;
            usize i = 0;
            for (; i < text.Size() && i < 31; ++i) { e.text[i] = text[i]; }
            e.text[i] = 0;
            events.PushBack(e);
        }
    };
}

TEST_CASE("ui.subsystem: key/text events reach a focused game EditText; IME follows focus")
{
    rt::Context ctx;
    auto* scenes = ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* input = ctx.AddSubsystem<draconic::input::InputSubsystem>(nullptr);
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();

    EventFakeDevices devices;
    input->SetSourceProvider(&devices);

    // The player-window IME target (headless stand-in).
    draconic::shell::NullWindow window(1, draconic::shell::WindowSettings{});
    ui->SetTextInputTarget(&window);

    dscene::Scene* scene = scenes->CreateScene(u8"menu");
    dscene::EntityHandle e = scene->CreateEntity(u8"form");
    {
        UICanvasComponent& c = scene->GetSystem<UICanvasComponentManager>()->Add(e);
        c.document = MakeDocument(
            u8"<Flex direction=\"vertical\">"
            u8"<EditText id=\"name-field\" width=\"200\" height=\"30\"/>"
            u8"</Flex>");
    }
    ctx.BeginFrame(1.0f / 60.0f);
    RootView* root = ui->SceneRoot(*scene);
    REQUIRE(root != nullptr);
    root->ViewportSize = Float2{ 800.0f, 600.0f };
    ui->Context().UpdateRootView(root);

    auto* edit = Cast<ViewGroup>(root)->FindByName<EditText>(u8"name-field");
    REQUIRE(edit != nullptr);

    // Nothing focused: no IME, keyboard not consumed.
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK_FALSE(window.IsTextInputActive());
    CHECK_FALSE(input->Runtime().GetConsumptionMask().keyboard);

    // Focus the field: the next pump starts platform text input (WantsTextInput went
    // on) and publishes the keyboard consumption class.
    ui->Context().GetFocusManager()->SetFocus(edit);
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(window.IsTextInputActive());
    CHECK(input->Runtime().GetConsumptionMask().keyboard);

    // Text events flow through the provider's event stream into the editor...
    devices.PushText(u8"hi");
    ctx.BeginFrame(1.0f / 60.0f);
    devices.events.Clear();
    CHECK(edit->Text() == u8"hi");

    // ...as do ordered key events (Backspace erases the last character).
    devices.PushKey(draconic::shell::InputEventKind::KeyDown, draconic::shell::KeyCode::Backspace);
    devices.PushKey(draconic::shell::InputEventKind::KeyUp, draconic::shell::KeyCode::Backspace);
    ctx.BeginFrame(1.0f / 60.0f);
    devices.events.Clear();
    CHECK(edit->Text() == u8"h");

    // Dropping focus stops text input and releases the keyboard class.
    ui->Context().GetFocusManager()->ClearFocus();
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK_FALSE(window.IsTextInputActive());
    CHECK_FALSE(input->Runtime().GetConsumptionMask().keyboard);

    ctx.Shutdown();
}

TEST_CASE("ui.subsystem: RenderTexture canvases own an offscreen target and stay out of the tiers")
{
    rt::Context ctx;
    auto* scenes = ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();

    // Headless GPU: the Null RHI device (texture lifecycle without a real GPU).
    draconic::rhi::null::NullDevice device{ DefaultAllocator() };
    ui->EnsureRenderReady(device, 2);
    draconic::rhi::null::NullCommandEncoder encoder;

    dscene::Scene* scene = scenes->CreateScene(u8"world");
    auto* canvases = scene->GetSystem<UICanvasComponentManager>();
    dscene::EntityHandle e = scene->CreateEntity(u8"screen");
    {
        UICanvasComponent& c = canvases->Add(e);
        c.document = MakeDocument(u8"<Label id=\"rt-label\" text=\"scoreboard\"/>");
        c.renderMode = CanvasRenderMode::RenderTexture;
        c.renderTextureWidth = 256;
        c.renderTextureHeight = 128;
    }
    const usize rootsBefore = ui->Context().RootViewCount();   // screen + scene roots
    ctx.BeginFrame(1.0f / 60.0f);
    UICanvasComponent* c = canvases->Get(e);
    REQUIRE(c != nullptr);
    REQUIRE(c->root.Get() != nullptr);
    REQUIRE(c->renderRoot.Get() != nullptr);
    CHECK(ui->Context().RootViewCount() == rootsBefore + 1);   // the standalone RT root

    // NOT in the overlay tiers: the scene root holds only the billboard layer, and the
    // document is unreachable from it (RT canvases never draw in the overlay pass and
    // never take pointer input - the input pump only probes tier roots).
    RootView* sceneRoot = ui->SceneRoot(*scene);
    REQUIRE(sceneRoot != nullptr);
    CHECK(sceneRoot->ChildCount() == 1u);
    CHECK(Cast<ViewGroup>(sceneRoot)->FindByName(u8"rt-label") == nullptr);
    CHECK(Cast<ViewGroup>(ui->ScreenRoot())->FindByName(u8"rt-label") == nullptr);
    CHECK(Cast<ViewGroup>(c->renderRoot.Get())->FindByName(u8"rt-label") != nullptr);

    // No texture until the host seam runs; then create at the authored size.
    CHECK(c->renderTexture == nullptr);
    ui->RenderCanvasTextures(encoder, 0);
    c = canvases->Get(e);
    REQUIRE(c->renderTexture != nullptr);
    REQUIRE(c->renderTextureView != nullptr);
    CHECK(c->renderTexture->desc.width == 256u);
    CHECK(c->renderTexture->desc.height == 128u);
    CHECK(ui->CanvasRenderTextureView(*scene, e) == c->renderTextureView);
    // The root laid out at the texture size.
    CHECK(c->renderRoot->ViewportSize.x == doctest::Approx(256.0f));
    CHECK(c->renderRoot->ViewportSize.y == doctest::Approx(128.0f));

    // Resize: the target recreates at the new size. (A fresh UI frame first - the
    // seam runs at most once per frame so co-hosted editor pages share one draw.)
    c->renderTextureWidth = 512;
    ctx.BeginFrame(1.0f / 60.0f);
    ui->RenderCanvasTextures(encoder, 1);
    c = canvases->Get(e);
    REQUIRE(c->renderTexture != nullptr);
    CHECK(c->renderTexture->desc.width == 512u);
    CHECK(c->renderTexture->desc.height == 128u);

    // Mode flip back to ScreenOverlay: the standalone root goes away, the document
    // re-parents into the scene root, and the GPU target is swept.
    c->renderMode = CanvasRenderMode::ScreenOverlay;
    ctx.BeginFrame(1.0f / 60.0f);
    c = canvases->Get(e);
    CHECK(c->renderRoot.Get() == nullptr);
    CHECK(c->renderTexture == nullptr);
    CHECK(c->renderTextureView == nullptr);
    CHECK(Cast<ViewGroup>(ui->SceneRoot(*scene))->FindByName(u8"rt-label") != nullptr);
    ui->RenderCanvasTextures(encoder, 0);
    CHECK(ui->CanvasRenderTextureView(*scene, e) == nullptr);

    // And back to RenderTexture, then DESPAWN: the sweep destroys the orphaned target
    // AND unregisters the standalone root from the context (which stores roots
    // non-owning - a stale registration would dangle).
    c->renderMode = CanvasRenderMode::RenderTexture;
    ctx.BeginFrame(1.0f / 60.0f);
    ui->RenderCanvasTextures(encoder, 0);
    REQUIRE(canvases->Get(e)->renderTexture != nullptr);
    CHECK(ui->Context().RootViewCount() == rootsBefore + 1);
    scene->DestroyEntity(e);
    ctx.BeginFrame(1.0f / 60.0f);
    ui->RenderCanvasTextures(encoder, 0);   // sweeps; must not crash or leak
    CHECK(canvases->Get(e) == nullptr);
    CHECK(ui->Context().RootViewCount() == rootsBefore);   // RT root swept with it

    ctx.Shutdown();
}

TEST_CASE("ui.subsystem: the project-default theme swaps the context stylesheet (GameTheme fallback)")
{
    rt::Context ctx;
    ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();

    StyleSheet* builtin = ui->Context().GetStyleSheet();
    REQUIRE(builtin != nullptr);   // the built-in GameTheme ships by default

    // A cooked UITheme (validated .sss text) replaces the context stylesheet.
    UITheme theme;
    theme.stylesheet = String(u8"Button { text-color: #ff0000; }");
    ui->SetDefaultTheme(&theme);
    StyleSheet* custom = ui->Context().GetStyleSheet();
    REQUIRE(custom != nullptr);
    CHECK(custom != builtin);

    // Null (nil manifest reference / cleared) restores the built-in GameTheme.
    ui->SetDefaultTheme(nullptr);
    StyleSheet* restored = ui->Context().GetStyleSheet();
    REQUIRE(restored != nullptr);
    CHECK(restored != custom);

    // The built-in LIGHT variant exists alongside GameTheme and differs in palette.
    RefPtr<StyleSheet> light = GameLightTheme::Create();
    CHECK(light.Get() != nullptr);
    CHECK(GameLightTheme::Palette().Background.r != doctest::Approx(GameTheme::Palette().Background.r));

    ctx.Shutdown();
}

TEST_CASE("ui.subsystem: removing a canvas or billboard COMPONENT sweeps its tree (user repro)")
{
    // The user's exact repro: add a ui.Canvas in the editor, see it render, REMOVE the
    // component (entity stays) - the UI must disappear. Same for billboards, whose
    // layer needed its own sweep.
    rt::Context ctx;
    auto* scenes = ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();
    dscene::Scene* scene = scenes->CreateScene(u8"level");

    dscene::EntityHandle e = scene->CreateEntity(u8"hud");
    auto* canvases = scene->GetSystem<UICanvasComponentManager>();
    canvases->Add(e).document = MakeDocument(u8"<Label id=\"hud-label\" text=\"HUD\"/>");
    auto* billboards = scene->GetSystem<UIBillboardComponentManager>();
    billboards->Add(e).document = MakeDocument(u8"<Label id=\"plate\" text=\"name\"/>");
    ctx.BeginFrame(1.0f / 60.0f);
    RootView* root = ui->SceneRoot(*scene);
    REQUIRE(root != nullptr);
    REQUIRE(Cast<ViewGroup>(root)->FindByName(u8"hud-label") != nullptr);
    REQUIRE(Cast<ViewGroup>(root)->FindByName(u8"plate") != nullptr);

    // Remove ONLY the components; the entity survives.
    canvases->Remove(e);
    billboards->Remove(e);
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(Cast<ViewGroup>(root)->FindByName(u8"hud-label") == nullptr);
    CHECK(Cast<ViewGroup>(root)->FindByName(u8"plate") == nullptr);

    ctx.Shutdown();
}

namespace
{
    // A pointer-capable provider fake: settable position + button state for the polled
    // pump, plus the tagged event stream - the full shape a real source presents.
    struct PointerFakeMouse final : draconic::shell::IMouse
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        bool buttons[8] = {};
        [[nodiscard]] f32 X() const override { return x; }
        [[nodiscard]] f32 Y() const override { return y; }
        [[nodiscard]] f32 GlobalX() const override { return x; }
        [[nodiscard]] f32 GlobalY() const override { return y; }
        [[nodiscard]] f32 DeltaX() const override { return 0.0f; }
        [[nodiscard]] f32 DeltaY() const override { return 0.0f; }
        [[nodiscard]] f32 ScrollX() const override { return 0.0f; }
        [[nodiscard]] f32 ScrollY() const override { return 0.0f; }
        [[nodiscard]] bool IsButtonDown(draconic::shell::MouseButton b) const override
        { return buttons[static_cast<u32>(b) & 7]; }
        [[nodiscard]] bool IsButtonPressed(draconic::shell::MouseButton) const override { return false; }
        [[nodiscard]] bool IsButtonReleased(draconic::shell::MouseButton) const override { return false; }
        [[nodiscard]] bool RelativeMode() const override { return false; }
        void SetRelativeMode(bool) override {}
        [[nodiscard]] bool CursorVisible() const override { return true; }
        void SetCursorVisible(bool) override {}
        void SetCursor(draconic::shell::CursorType) override {}
        void SetGlobalCapture(bool) override {}
    };

    struct PointerFakeDevices final : draconic::input::IInputSourceProvider
    {
        PointerFakeMouse mouse;
        bool mousePresent = true;   // false = pointer-less frame (pad/keyboard-only path)
        Array<draconic::shell::InputEvent> events;
        [[nodiscard]] draconic::shell::IMouse* Mouse() override
        { return mousePresent ? &mouse : nullptr; }
        [[nodiscard]] draconic::shell::IKeyboard* Keyboard() override { return nullptr; }
        [[nodiscard]] i32 GamepadCount() const override { return 0; }
        [[nodiscard]] draconic::shell::IGamepad* Gamepad(i32) override { return nullptr; }
        [[nodiscard]] Span<const draconic::shell::InputEvent> Events() override
        {
            return { events.Data(), events.Size() };
        }
        void PushText(StringView text)
        {
            draconic::shell::InputEvent e;
            e.kind = draconic::shell::InputEventKind::TextInput;
            usize i = 0;
            for (; i < text.Size() && i < 31; ++i) { e.text[i] = text[i]; }
            e.text[i] = 0;
            events.PushBack(e);
        }
        // One full click at the CURRENT position across two pump frames.
        void PressLeft() { mouse.buttons[static_cast<u32>(draconic::shell::MouseButton::Left)] = true; }
        void ReleaseLeft() { mouse.buttons[static_cast<u32>(draconic::shell::MouseButton::Left)] = false; }
    };
}

TEST_CASE("ui.subsystem: a bound source confines routing + consumption to ITS scene (game-ui.md §9)")
{
    rt::Context ctx;
    auto* scenes = ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* input = ctx.AddSubsystem<draconic::input::InputSubsystem>(nullptr);
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();

    PointerFakeDevices devices;
    input->SetSourceProvider(&devices);   // un-bound: the AllScenes default applies

    // Two scenes, each with an interactive button at the SAME coordinates - the
    // historical known edge (probing in creation order can hit the wrong scene).
    dscene::Scene* sceneA = scenes->CreateScene(u8"a");
    dscene::EntityHandle ea = sceneA->CreateEntity(u8"hud-a");
    sceneA->GetSystem<UICanvasComponentManager>()->Add(ea).document = MakeDocument(
        u8"<Flex direction=\"vertical\"><Button id=\"btn-a\" text=\"A\" width=\"200\" height=\"40\"/></Flex>");
    dscene::Scene* sceneB = scenes->CreateScene(u8"b");
    dscene::EntityHandle eb = sceneB->CreateEntity(u8"hud-b");
    sceneB->GetSystem<UICanvasComponentManager>()->Add(eb).document = MakeDocument(
        u8"<Flex direction=\"vertical\">"
        u8"<Button id=\"btn-b\" text=\"B\" width=\"200\" height=\"40\"/>"
        u8"<EditText id=\"field-b\" width=\"200\" height=\"30\"/>"
        u8"</Flex>");

    ctx.BeginFrame(1.0f / 60.0f);   // instantiate trees
    RootView* rootA = ui->SceneRoot(*sceneA);
    RootView* rootB = ui->SceneRoot(*sceneB);
    REQUIRE(rootA != nullptr);
    REQUIRE(rootB != nullptr);
    rootA->ViewportSize = Float2{ 800.0f, 600.0f };
    rootB->ViewportSize = Float2{ 800.0f, 600.0f };
    ui->Context().UpdateRootView(rootA);
    ui->Context().UpdateRootView(rootB);

    bool clickedA = false;
    bool clickedB = false;
    Cast<ViewGroup>(rootA)->FindByName<Button>(u8"btn-a")->OnClick.Add(
        [&clickedA](ButtonBase*) { clickedA = true; });
    Cast<ViewGroup>(rootB)->FindByName<Button>(u8"btn-b")->OnClick.Add(
        [&clickedB](ButtonBase*) { clickedB = true; });

    // UN-BOUND (AllScenes default): the pointer probe walks scene roots in creation
    // order - scene A wins the overlapping point. (The documented historical edge.)
    devices.mouse.x = 50.0f;
    devices.mouse.y = 20.0f;
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(ui->Context().ActiveInputRoot() == rootA);
    CHECK(input->Runtime().GetConsumptionMask().pointer);

    // BOUND to scene B: the same coordinates now route to B - and ONLY B. The click
    // fires B's button; A's identical button at the identical point never hears it.
    input->SetSourceProvider(&devices, sceneB);
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(ui->Context().ActiveInputRoot() == rootB);
    CHECK(input->Runtime().GetConsumptionMask().pointer);
    devices.PressLeft();
    ctx.BeginFrame(1.0f / 60.0f);
    devices.ReleaseLeft();
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(clickedB);
    CHECK_FALSE(clickedA);

    // Pointer-less frames (pad/keyboard-only): the fallback root is the BOUND scene,
    // not the first scene with content.
    devices.mousePresent = false;
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(ui->Context().ActiveInputRoot() == rootB);

    // Keyboard/text follow the binding: focus B's field, stream text, and the
    // keyboard consumption class publishes for the bound scene.
    auto* field = Cast<ViewGroup>(rootB)->FindByName<EditText>(u8"field-b");
    REQUIRE(field != nullptr);
    ui->Context().GetFocusManager()->SetFocus(field);
    devices.PushText(u8"go");
    ctx.BeginFrame(1.0f / 60.0f);
    devices.events.Clear();
    CHECK(field->Text() == u8"go");
    CHECK(input->Runtime().GetConsumptionMask().keyboard);
    ui->Context().GetFocusManager()->ClearFocus();

    // Un-binding (provider kept) returns to the un-bound default: creation order.
    devices.mousePresent = true;
    input->SetSourceProvider(&devices, nullptr);
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(ui->Context().ActiveInputRoot() == rootA);

    ctx.Shutdown();
}

TEST_CASE("ui.subsystem: ScreenTierOnly keeps un-bound input out of scene UI (editor policy)")
{
    rt::Context ctx;
    auto* scenes = ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* input = ctx.AddSubsystem<draconic::input::InputSubsystem>(nullptr);
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();

    PointerFakeDevices devices;
    input->SetSourceProvider(&devices);   // un-bound...
    input->SetUnboundScenePolicy(draconic::input::UnboundInputScenePolicy::ScreenTierOnly);

    dscene::Scene* scene = scenes->CreateScene(u8"editing");
    dscene::EntityHandle e = scene->CreateEntity(u8"hud");
    scene->GetSystem<UICanvasComponentManager>()->Add(e).document = MakeDocument(
        u8"<Flex direction=\"vertical\"><Button id=\"btn\" text=\"hud\" width=\"200\" height=\"40\"/></Flex>");
    ctx.BeginFrame(1.0f / 60.0f);
    RootView* root = ui->SceneRoot(*scene);
    REQUIRE(root != nullptr);
    root->ViewportSize = Float2{ 800.0f, 600.0f };
    ui->ScreenRoot()->ViewportSize = Float2{ 800.0f, 600.0f };
    ui->Context().UpdateRootView(root);
    ui->Context().UpdateRootView(ui->ScreenRoot());

    bool clicked = false;
    Cast<ViewGroup>(root)->FindByName<Button>(u8"btn")->OnClick.Add(
        [&clicked](ButtonBase*) { clicked = true; });

    // The HUD renders (tree exists, visible) but is NOT interactive: the pointer over
    // its button neither routes to the scene root nor publishes consumption - an
    // editor pane click at these coordinates stays an editor click.
    devices.mouse.x = 50.0f;
    devices.mouse.y = 20.0f;
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(ui->Context().ActiveInputRoot() == ui->ScreenRoot());
    CHECK_FALSE(input->Runtime().GetConsumptionMask().pointer);
    CHECK_FALSE(ui->PointerOverUI());
    devices.PressLeft();
    ctx.BeginFrame(1.0f / 60.0f);
    devices.ReleaseLeft();
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK_FALSE(clicked);

    // The scene-less screen tier is NOT confined: an occupied overlay is modal and
    // fully interactive under the same policy (loading screens/system menus work).
    RefPtr<UIDocument> modal = MakeRef<UIDocument>(DefaultAllocator());
    modal->markup = String(
        u8"<Flex direction=\"vertical\"><Button id=\"ok\" text=\"OK\" width=\"200\" height=\"40\"/></Flex>");
    RefPtr<View> overlay = ui->PushScreenOverlay(*modal);
    REQUIRE(overlay.Get() != nullptr);
    ctx.BeginFrame(1.0f / 60.0f);
    ui->Context().UpdateRootView(ui->ScreenRoot());
    bool okClicked = false;
    Cast<ViewGroup>(ui->ScreenRoot())->FindByName<Button>(u8"ok")->OnClick.Add(
        [&okClicked](ButtonBase*) { okClicked = true; });
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(ui->Context().ActiveInputRoot() == ui->ScreenRoot());
    CHECK(input->Runtime().GetConsumptionMask().pointer);
    devices.PressLeft();
    ctx.BeginFrame(1.0f / 60.0f);
    devices.ReleaseLeft();
    ctx.BeginFrame(1.0f / 60.0f);
    CHECK(okClicked);
    CHECK_FALSE(clicked);   // the scene button under the overlay still never fires
    ui->RemoveScreenOverlay(overlay.Get());

    ctx.Shutdown();
}

TEST_CASE("ui.subsystem: RT canvases auto-bind the entity's sprite/decal texture override")
{
    rt::Context ctx;
    auto* scenes = ctx.AddSubsystem<dscene::SceneSubsystem>();
    auto* ui = ctx.AddSubsystem<UISubsystem>();
    ctx.Startup();

    draconic::rhi::null::NullDevice device{ DefaultAllocator() };
    ui->EnsureRenderReady(device, 2);
    draconic::rhi::null::NullCommandEncoder encoder;

    // The render managers normally come from the RenderSubsystem; add them directly
    // (the camera-manager test's pattern).
    dscene::Scene* scene = scenes->CreateScene(u8"world");
    scene->AddSystem<draconic::render::SpriteComponentManager>();
    scene->AddSystem<draconic::render::DecalComponentManager>();
    auto* canvases = scene->GetSystem<UICanvasComponentManager>();
    auto* sprites = scene->GetSystem<draconic::render::SpriteComponentManager>();
    auto* decals = scene->GetSystem<draconic::render::DecalComponentManager>();

    // One entity carries the RT canvas AND the material components that show it -
    // the declarative contract: same entity = auto-bound, no scripting needed.
    dscene::EntityHandle e = scene->CreateEntity(u8"scoreboard");
    {
        UICanvasComponent& c = canvases->Add(e);
        c.document = MakeDocument(u8"<Label id=\"score\" text=\"0 : 0\"/>");
        c.renderMode = CanvasRenderMode::RenderTexture;
        c.renderTextureWidth = 256;
        c.renderTextureHeight = 128;
    }
    sprites->Add(e);
    decals->Add(e);

    ctx.BeginFrame(1.0f / 60.0f);
    ui->RenderCanvasTextures(encoder, 0);
    UICanvasComponent* c = canvases->Get(e);
    REQUIRE(c != nullptr);
    REQUIRE(c->renderTextureView != nullptr);
    CHECK(sprites->Get(e)->texture == c->renderTextureView);
    CHECK(decals->Get(e)->texture == c->renderTextureView);

    // Resize recreates the target - the view pointer changes (the reason manual
    // assignment breaks) - and the binder refreshes the overrides the same call.
    // (No pointer-inequality check: an allocator may legitimately reuse the address;
    // the CONTRACT is override == current view, whatever that is.)
    c->renderTextureWidth = 512;
    ctx.BeginFrame(1.0f / 60.0f);
    ui->RenderCanvasTextures(encoder, 1);
    c = canvases->Get(e);
    REQUIRE(c->renderTextureView != nullptr);
    CHECK(c->renderTexture->desc.width == 512u);
    CHECK(sprites->Get(e)->texture == c->renderTextureView);
    CHECK(decals->Get(e)->texture == c->renderTextureView);

    // Removing the CANVAS un-binds (the target is destroyed - a stale override would
    // dangle); the sprite/decal components themselves survive.
    canvases->Remove(e);
    ctx.BeginFrame(1.0f / 60.0f);
    ui->RenderCanvasTextures(encoder, 0);
    CHECK(sprites->Get(e)->texture == nullptr);
    CHECK(decals->Get(e)->texture == nullptr);

    ctx.Shutdown();
}
