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
    CHECK(ui->ScreenRoot()->ChildCount() == 1u);

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
