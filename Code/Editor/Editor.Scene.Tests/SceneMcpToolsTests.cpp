// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::Scene tests - the scene editor's MCP tools over a real EditorContext holding pages
// that publish ISceneEditorPage (a headless scene + edit context behind each) beside one that
// does not: page addressing (the active page, an explicit page, a page that is not a scene),
// the selection round-trip with names and the primary, the refusals for unknown entities and
// pages, and the simulate control reflecting the page's state.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.json;
import foundation.mcp;
import foundation.scene;
import foundation.resource;
import foundation.materials;
import engine.render;
import editor.core;
import editor.scene;
import editor.camera;

using namespace foundation::core;
using namespace foundation::mcp;
using namespace editor;
namespace scene = foundation::scene;
namespace json = foundation::json;
using json::JsonValue;

namespace
{
    // A page that IS a scene page to the rest of the editor: a headless scene and its edit
    // context, the simulate state as flags. No UI, no viewport.
    class HeadlessScenePage final : public EditorPage, public ISceneEditorPage
    {
    public:
        HeadlessScenePage(StringView title, const Guid& asset)
            : EditorPage(DefaultAllocator()), m_title(title),
              m_scene(DefaultAllocator(), u8"headless"), m_edit(m_scene, Commands())
        {
            SetInstanceId(asset);
            Provide<ISceneEditorPage>(*this);
        }
        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] Status Save() override { return Status{}; }
        [[nodiscard]] SceneEditContext& EditContext() noexcept override { return m_edit; }
        void StartSimulation() override { m_simulating = true; }
        void StopSimulation() override { m_simulating = false; }
        void PauseSimulation(bool) override {}
        [[nodiscard]] bool IsSimulating() const noexcept override { return m_simulating; }
        [[nodiscard]] bool IsPaused() const noexcept override { return false; }
        [[nodiscard]] GizmoController* Gizmos() noexcept override { return nullptr; }
        [[nodiscard]] bool CameraOwnsInput() const noexcept override { return false; }
        [[nodiscard]] bool MarkersShown() const noexcept override { return true; }
        // A viewport is pretended when `hasViewport`: the camera is real, the capture advances
        // when the test says the frame rendered (CompleteCapture / FailCapture).
        [[nodiscard]] EditorCamera* ViewportCamera() noexcept override { return hasViewport ? &camera : nullptr; }
        [[nodiscard]] Status RequestViewportCapture(StringView path) override
        {
            if (!hasViewport)
            {
                return Status{ErrorCode::NotSupported};
            }
            capture = ViewportCapture{};
            capture.state = ViewportCaptureState::Pending;
            capture.path = String(path);
            ++captureRequests;
            return Status{};
        }
        [[nodiscard]] const ViewportCapture& LastViewportCapture() const noexcept override { return capture; }
        void CompleteCapture(u32 width, u32 height)
        {
            capture.state = ViewportCaptureState::Written;
            capture.width = width;
            capture.height = height;
        }
        void FailCapture() { capture.state = ViewportCaptureState::Failed; }
        bool hasViewport = false;
        EditorCamera camera;
        ViewportCapture capture;
        u32 captureRequests = 0;
        void SetMarkersShown(bool) override {}
        [[nodiscard]] bool AnimationPanelShown() const noexcept override { return false; }
        void SetAnimationPanelShown(bool) override {}
        void CreatePrefabFromEntity(const Guid&) override {}
        void PickAndSpawnPrefab(const Guid&) override {}
        void ApplyInstanceToPrefab(const Guid&) override {}
        void RevertInstance(const Guid&) override {}

    private:
        String m_title;
        scene::Scene m_scene;
        SceneEditContext m_edit; // over the page's own stack, as the real page's is
        bool m_simulating = false;
    };

    class PlainPage final : public EditorPage
    {
    public:
        explicit PlainPage(const Guid& asset) : EditorPage(DefaultAllocator())
        {
            SetInstanceId(asset);
        }
        [[nodiscard]] StringView Title() const override { return u8"a material"; }
        [[nodiscard]] Status Save() override { return Status{}; }
    };

    struct Answer
    {
        bool ok = false;
        JsonValue payload;
        String error;
    };
    Answer Call(McpServer& server, StringView tool, StringView argumentsJson)
    {
        const String line = Format(u8"{{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\","
                                   u8"\"params\":{{\"name\":\"{}\",\"arguments\":{}}}}}",
                                   tool, argumentsJson);
        LineOutcome outcome = server.HandleLine(line.AsView());
        REQUIRE(outcome.state == LineState::Answered);
        JsonValue result = json::Parse(outcome.response.AsView()).value.Get(u8"result");
        Answer answer;
        answer.ok = !result.Get(u8"isError").AsBool();
        const String text = result.Get(u8"content").At(0).Get(u8"text").AsString();
        if (answer.ok)
        {
            answer.payload = json::Parse(text.AsView()).value;
        }
        else
        {
            answer.error = text;
        }
        return answer;
    }

    String GuidText(const Guid& id)
    {
        utf8char text[37];
        id.ToChars(text);
        return String(StringView(text, 36));
    }

    /// One pump of a tool that may ask to be re-entered: the raw line outcome.
    LineOutcome Pump(McpServer& server, StringView tool, StringView argumentsJson)
    {
        const String line = Format(u8"{{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\","
                                   u8"\"params\":{{\"name\":\"{}\",\"arguments\":{}}}}}",
                                   tool, argumentsJson);
        return server.HandleLine(line.AsView());
    }
    Answer AnswerOf(const LineOutcome& outcome)
    {
        REQUIRE(outcome.state == LineState::Answered);
        JsonValue result = json::Parse(outcome.response.AsView()).value.Get(u8"result");
        Answer answer;
        answer.ok = !result.Get(u8"isError").AsBool();
        const String text = result.Get(u8"content").At(0).Get(u8"text").AsString();
        if (answer.ok)
        {
            answer.payload = json::Parse(text.AsView()).value;
        }
        else
        {
            answer.error = text;
        }
        return answer;
    }
}

TEST_CASE("scene-mcp-tools: page addressing, the selection round-trip, its refusals, and the "
          "simulate control")
{
    Random rng(7);
    const Guid sceneA = Guid::Generate(rng);
    const Guid sceneB = Guid::Generate(rng);
    const Guid material = Guid::Generate(rng);

    EditorContext context{DefaultAllocator()};
    auto* pageA = static_cast<HeadlessScenePage*>(context.AdoptPage(UniquePtr<EditorPage>(
        DefaultAllocator().New<HeadlessScenePage>(u8"Bistro", sceneA), DefaultAllocator())));
    auto* pageB = static_cast<HeadlessScenePage*>(context.AdoptPage(UniquePtr<EditorPage>(
        DefaultAllocator().New<HeadlessScenePage>(u8"Menu", sceneB), DefaultAllocator())));
    EditorPage* plain = context.AdoptPage(UniquePtr<EditorPage>(
        DefaultAllocator().New<PlainPage>(material), DefaultAllocator()));
    REQUIRE(context.OpenPages().Size() == 3u);
    const Guid lamp = pageA->EditContext().CreateEntity(u8"Lamp");
    const Guid table = pageA->EditContext().CreateEntity(u8"Table");
    // The other page's entity is minted straight on its scene with its own guid (a page's
    // CreateEntity also selects, and two fresh scenes may mint the same first guid).
    const Guid otherSceneEntity = Guid::Generate(rng);
    (void)pageB->EditContext().Scene().CreateEntity(otherSceneEntity, u8"Elsewhere");
    pageA->EditContext().EntitySelection().Set(Span<const Guid>{});

    McpServer server;
    RegisterSceneLiveTools(server, context);
    CHECK(server.ToolCount() == kSceneLiveToolCount);
    CHECK(kSceneLiveToolCount == 9u);
    const String aGuid = GuidText(sceneA);
    const String lampGuid = GuidText(lamp);
    const String tableGuid = GuidText(table);

    // Default addressing: the active page - a scene page, then a page that is not one.
    context.SetActivePage(pageA);
    Answer got = Call(server, u8"selection_get", u8"{}");
    REQUIRE(got.ok);
    CHECK(got.payload.Get(u8"page").Get(u8"title").AsString() == StringView(u8"Bistro"));
    CHECK(got.payload.Get(u8"entities").Count() == 0);
    CHECK(got.payload.Get(u8"primary").IsNull());
    context.SetActivePage(plain);
    got = Call(server, u8"selection_get", u8"{}");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"page 'a material' is not a scene or prefab page"));
    // Explicit addressing reaches a scene page whatever is active; unknown pages refuse.
    got = Call(server, u8"selection_get", Format(u8"{{\"page\":\"{}\"}}", aGuid.AsView()).AsView());
    REQUIRE(got.ok);
    CHECK(got.payload.Get(u8"page").Get(u8"title").AsString() == StringView(u8"Bistro"));
    got = Call(server, u8"selection_get", u8"{\"page\":\"00000000-0000-0000-0000-000000000000\"}");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"no open page for guid"));

    // The selection round-trip: order kept, the first is the primary, names resolved.
    Answer set = Call(server, u8"selection_set",
                      Format(u8"{{\"page\":\"{}\",\"entities\":[\"{}\",\"{}\"]}}", aGuid.AsView(),
                             tableGuid.AsView(), lampGuid.AsView())
                          .AsView());
    REQUIRE(set.ok);
    REQUIRE(set.payload.Get(u8"entities").Count() == 2);
    CHECK(set.payload.Get(u8"entities").At(0).Get(u8"name").AsString() == StringView(u8"Table"));
    CHECK(set.payload.Get(u8"entities").At(1).Get(u8"name").AsString() == StringView(u8"Lamp"));
    CHECK(set.payload.Get(u8"primary").AsString() == tableGuid.AsView());
    REQUIRE(pageA->EditContext().EntitySelection().Items().Size() == 2u);
    CHECK(*pageA->EditContext().EntitySelection().Primary() == table);
    CHECK(pageB->EditContext().EntitySelection().IsEmpty()); // the other page is untouched
    // An entity of ANOTHER page's scene is refused for this page; nothing changes.
    Answer wrong = Call(server, u8"selection_set",
                        Format(u8"{{\"page\":\"{}\",\"entities\":[\"{}\"]}}", aGuid.AsView(),
                               GuidText(otherSceneEntity).AsView())
                            .AsView());
    CHECK_FALSE(wrong.ok);
    CHECK(wrong.error.AsView().StartsWith(u8"no entity with guid"));
    CHECK(pageA->EditContext().EntitySelection().Items().Size() == 2u);
    // An empty list clears.
    set = Call(server, u8"selection_set",
               Format(u8"{{\"page\":\"{}\",\"entities\":[]}}", aGuid.AsView()).AsView());
    REQUIRE(set.ok);
    CHECK(pageA->EditContext().EntitySelection().IsEmpty());

    // Simulate reflects the page's state and addresses the same way.
    Answer sim = Call(server, u8"simulate_start", Format(u8"{{\"page\":\"{}\"}}", aGuid.AsView()).AsView());
    REQUIRE(sim.ok);
    CHECK(sim.payload.Get(u8"simulating").AsBool());
    CHECK(pageA->IsSimulating());
    CHECK_FALSE(pageB->IsSimulating());
    sim = Call(server, u8"simulate_stop", Format(u8"{{\"page\":\"{}\"}}", aGuid.AsView()).AsView());
    REQUIRE(sim.ok);
    CHECK_FALSE(sim.payload.Get(u8"simulating").AsBool());
    CHECK_FALSE(pageA->IsSimulating());
    context.SetActivePage(plain);
    sim = Call(server, u8"simulate_start", u8"{}");
    CHECK_FALSE(sim.ok);

    context.ClosePage(plain);
    context.ClosePage(pageB);
    context.ClosePage(pageA);
}

TEST_CASE("scene-mcp-tools: entity_inspect reads an entity and its components through reflection - "
          "identity, hierarchy, transform, enums by name, references as guids, lists expanded, "
          "the primary selection as the default, and the refusals")
{
    engine::render::RegisterRenderComponentReflection();
    Random rng(21);
    const Guid sceneId = Guid::Generate(rng);
    EditorContext context{DefaultAllocator()};
    auto* page = static_cast<HeadlessScenePage*>(context.AdoptPage(UniquePtr<EditorPage>(
        DefaultAllocator().New<HeadlessScenePage>(u8"Bistro", sceneId), DefaultAllocator())));
    SceneEditContext& edit = page->EditContext();
    scene::Scene& scene = edit.Scene();
    auto* lights = scene.AddSystem<engine::render::LightComponentManager>();
    auto* meshes = scene.AddSystem<engine::render::MeshComponentManager>();

    const Guid lampId = edit.CreateEntity(u8"Lamp");
    const Guid bulbId = edit.CreateEntity(u8"Bulb", lampId);
    const scene::EntityHandle lamp = edit.Resolve(lampId);
    const scene::EntityHandle bulb = edit.Resolve(bulbId);
    engine::render::LightComponent& light = lights->Add(bulb);
    light.type = engine::render::LightType::Spot;
    light.intensity = 2.5f;
    light.color = Color{1.0f, 0.5f, 0.25f, 1.0f};
    engine::render::MeshComponent& mesh = meshes->Add(lamp);
    const Guid meshAsset = Guid::Generate(rng);
    const Guid materialAsset = Guid::Generate(rng);
    mesh.mesh.SetId(meshAsset);
    mesh.materials.PushBack(foundation::resource::Ref<foundation::materials::Material>{});
    mesh.materials[0].SetId(materialAsset);
    mesh.visible = false;
    Transform placed;
    placed.position = Float3{1.0f, 2.0f, 3.0f};
    scene.SetLocalTransform(lamp, placed);

    McpServer server;
    RegisterSceneLiveTools(server, context);
    const String pageGuid = GuidText(sceneId);

    // By guid: the lamp with its child, its transform, and the mesh's references and list.
    Answer got = Call(server, u8"entity_inspect",
                      Format(u8"{{\"page\":\"{}\",\"entity\":\"{}\"}}", pageGuid.AsView(),
                             GuidText(lampId).AsView())
                          .AsView());
    REQUIRE(got.ok);
    const JsonValue entity = got.payload.Get(u8"entity");
    CHECK(entity.Get(u8"name").AsString() == StringView(u8"Lamp"));
    CHECK(entity.Get(u8"active").AsBool());
    CHECK(entity.Get(u8"parent").IsNull());
    REQUIRE(entity.Get(u8"children").Count() == 1);
    CHECK(entity.Get(u8"children").At(0).AsString() == GuidText(bulbId).AsView());
    CHECK(entity.Get(u8"transform").Get(u8"position").At(2).AsNumber() == doctest::Approx(3.0));
    REQUIRE(entity.Get(u8"components").Count() == 1);
    const JsonValue meshJson = entity.Get(u8"components").At(0);
    CHECK(meshJson.Get(u8"type").AsString() == StringView(u8"mesh"));
    CHECK(meshJson.Get(u8"typeName").AsString() == StringView(u8"MeshComponent"));
    const JsonValue meshProps = meshJson.Get(u8"properties");
    CHECK(meshProps.Get(u8"mesh").AsString() == GuidText(meshAsset).AsView());
    CHECK_FALSE(meshProps.Get(u8"visible").AsBool());
    REQUIRE(meshProps.Get(u8"materials").Count() == 1);
    CHECK(meshProps.Get(u8"materials").At(0).AsString() == GuidText(materialAsset).AsView());

    // The primary selection as the default: the bulb, a child, with its light's enum by name.
    edit.EntitySelection().Set(bulbId);
    got = Call(server, u8"entity_inspect", Format(u8"{{\"page\":\"{}\"}}", pageGuid.AsView()).AsView());
    REQUIRE(got.ok);
    const JsonValue bulbJson = got.payload.Get(u8"entity");
    CHECK(bulbJson.Get(u8"parent").AsString() == GuidText(lampId).AsView());
    REQUIRE(bulbJson.Get(u8"components").Count() == 1);
    const JsonValue lightProps = bulbJson.Get(u8"components").At(0).Get(u8"properties");
    CHECK(lightProps.Get(u8"type").AsString() == StringView(u8"Spot"));
    CHECK(lightProps.Get(u8"intensity").AsNumber() == doctest::Approx(2.5));
    REQUIRE(lightProps.Get(u8"color").Count() == 4);
    CHECK(lightProps.Get(u8"color").At(1).AsNumber() == doctest::Approx(0.5));

    // Refusals: no selection and no entity; an unknown entity.
    edit.EntitySelection().Clear();
    got = Call(server, u8"entity_inspect", Format(u8"{{\"page\":\"{}\"}}", pageGuid.AsView()).AsView());
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"page 'Bistro' has no selection"));
    got = Call(server, u8"entity_inspect",
               Format(u8"{{\"page\":\"{}\",\"entity\":\"00000000-0000-0000-0000-000000000001\"}}",
                      pageGuid.AsView())
                   .AsView());
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"no entity with guid"));

    context.ClosePage(page);
}

TEST_CASE("scene-mcp-tools: component_set writes one property through the undo path - leaves, an "
          "enum by name, a reference by guid - one locked step per call that Undo takes back, "
          "and the refusals leave nothing behind")
{
    engine::render::RegisterRenderComponentReflection();
    Random rng(33);
    const Guid sceneId = Guid::Generate(rng);
    EditorContext context{DefaultAllocator()};
    auto* page = static_cast<HeadlessScenePage*>(context.AdoptPage(UniquePtr<EditorPage>(
        DefaultAllocator().New<HeadlessScenePage>(u8"Bistro", sceneId), DefaultAllocator())));
    SceneEditContext& edit = page->EditContext();
    scene::Scene& scene = edit.Scene();
    auto* lights = scene.AddSystem<engine::render::LightComponentManager>();
    auto* meshes = scene.AddSystem<engine::render::MeshComponentManager>();
    const Guid lampId = edit.CreateEntity(u8"Lamp");
    const scene::EntityHandle lamp = edit.Resolve(lampId);
    engine::render::LightComponent& light = lights->Add(lamp);
    light.intensity = 1.0f;
    engine::render::MeshComponent& mesh = meshes->Add(lamp);
    (void)mesh;
    page->ClearDirty();
    edit.Commands().Clear();

    McpServer server;
    RegisterSceneLiveTools(server, context);
    const String pageGuid = GuidText(sceneId);
    const String lampGuid = GuidText(lampId);
    const auto set = [&](StringView component, StringView property, StringView valueJson)
    {
        return Call(server, u8"component_set",
                    Format(u8"{{\"page\":\"{}\",\"entity\":\"{}\",\"component\":\"{}\",\"property\":\"{}\","
                           u8"\"value\":{}}}",
                           pageGuid.AsView(), lampGuid.AsView(), component, property, valueJson)
                        .AsView());
    };

    // A float, by the component's serialization id; the page is dirty after, the value read
    // back as entity_inspect shows it.
    Answer got = set(u8"light", u8"intensity", u8"2.5");
    REQUIRE(got.ok);
    CHECK(lights->Get(lamp)->intensity == doctest::Approx(2.5f));
    CHECK(got.payload.Get(u8"value").AsNumber() == doctest::Approx(2.5));
    CHECK(got.payload.Get(u8"component").AsString() == StringView(u8"light"));
    CHECK(page->IsDirty());
    // An enum by name, by the type's name; a color as four numbers; a bool.
    REQUIRE(set(u8"LightComponent", u8"type", u8"\"Spot\"").ok);
    CHECK(lights->Get(lamp)->type == engine::render::LightType::Spot);
    REQUIRE(set(u8"light", u8"color", u8"[0.1,0.2,0.3,1]").ok);
    CHECK(lights->Get(lamp)->color.g == doctest::Approx(0.2f));
    REQUIRE(set(u8"mesh", u8"visible", u8"false").ok);
    CHECK_FALSE(meshes->Get(lamp)->visible);
    // A reference by guid (no resource manager in a headless page: the id is the write).
    const Guid meshAsset = Guid::Generate(rng);
    got = set(u8"mesh", u8"mesh", Format(u8"\"{}\"", GuidText(meshAsset).AsView()).AsView());
    REQUIRE(got.ok);
    CHECK(meshes->Get(lamp)->mesh.id == meshAsset);
    CHECK(got.payload.Get(u8"value").AsString() == GuidText(meshAsset).AsView());

    // Five writes, five undo steps: each Undo takes exactly one back, the reference first.
    REQUIRE(edit.Commands().CanUndo());
    edit.Commands().Undo();
    CHECK(meshes->Get(lamp)->mesh.id.IsNil());
    CHECK_FALSE(meshes->Get(lamp)->visible); // the previous step still stands
    edit.Commands().Undo();
    CHECK(meshes->Get(lamp)->visible);
    edit.Commands().Undo();
    CHECK(lights->Get(lamp)->color.g == doctest::Approx(1.0f));
    edit.Commands().Undo();
    CHECK(lights->Get(lamp)->type == engine::render::LightType::Directional);
    edit.Commands().Undo();
    CHECK(lights->Get(lamp)->intensity == doctest::Approx(1.0f));
    CHECK_FALSE(edit.Commands().CanUndo());
    // Two writes of the SAME property are still two steps (the user's scrubs merge; an
    // agent's calls do not).
    REQUIRE(set(u8"light", u8"intensity", u8"3").ok);
    REQUIRE(set(u8"light", u8"intensity", u8"4").ok);
    edit.Commands().Undo();
    CHECK(lights->Get(lamp)->intensity == doctest::Approx(3.0f));
    edit.Commands().Undo();
    CHECK(lights->Get(lamp)->intensity == doctest::Approx(1.0f));

    // Refusals, each leaving the value and the stack as they were.
    const i64 stackBefore = edit.Commands().UndoIndex();
    got = set(u8"light", u8"intensity", u8"\"bright\"");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"property 'intensity' of 'light' takes a number"));
    got = set(u8"light", u8"type", u8"\"Laser\"");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"property 'type' takes one of: Directional, Point, Spot"));
    got = set(u8"light", u8"brightness", u8"1");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"component 'light' has no property 'brightness'"));
    got = set(u8"physics.RigidBody", u8"mass", u8"1");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"entity 'Lamp' has no reflected component"));
    got = set(u8"mesh", u8"materials", u8"[]");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"property 'materials' of 'mesh' is a list"));
    got = set(u8"mesh", u8"mesh", u8"\"not-a-guid\"");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"property 'mesh' is a reference"));
    CHECK(edit.Commands().UndoIndex() == stackBefore);
    CHECK(lights->Get(lamp)->intensity == doctest::Approx(1.0f));
    // Simulating locks the edits.
    page->StartSimulation();
    got = set(u8"light", u8"intensity", u8"9");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"page 'Bistro' is simulating"));
    page->StopSimulation();
    CHECK(lights->Get(lamp)->intensity == doctest::Approx(1.0f));

    context.ClosePage(page);
}


namespace
{
    // A component with the shapes the render components do not offer: a string, a vector, and
    // a stored field the type publishes read-only.
    enum class PlaqueMood : i32
    {
        Calm = 0,
        Loud = 1,
        Wild = 5 // non-contiguous: a number names the enumerator's value, not its index
    };
    struct PlaqueComponent
    {
        String text{u8"untitled"};
        Float3 offset{0, 0, 0};
        Quaternion rotation{0, 0, 0, 1};
        scene::EntityRef target;
        PlaqueMood mood = PlaqueMood::Calm;
        i32 serial = 7;
    };
    class PlaqueManager final : public scene::ComponentManager<PlaqueComponent>
    {
    };
}

REFLECT_ENUM(PlaqueMood, "rtti::editor::scene::test")
{
    builder.Value("Calm", PlaqueMood::Calm);
    builder.Value("Loud", PlaqueMood::Loud);
    builder.Value("Wild", PlaqueMood::Wild);
}

REFLECT_VALUE(PlaqueComponent, "rtti::editor::scene::test")
{
    builder.Property<&PlaqueComponent::text>("text")
        .Property<&PlaqueComponent::offset>("offset")
        .Property<&PlaqueComponent::rotation>("rotation")
        .Property<&PlaqueComponent::target>("target")
        .Property<&PlaqueComponent::mood>("mood")
        .Property<&PlaqueComponent::serial>("serial", PropertyFlags::ReadOnly);
}

TEST_CASE("scene-mcp-tools: component_set writes a string, a vector, a quaternion, an entity reference "
          "(set and cleared), an enum by number, clears a reference with null, and refuses a "
          "read-only property before anything changes")
{
    RttiRegisterEnum_PlaqueMood();
    RttiRegisterValue_PlaqueComponent();
    engine::render::RegisterRenderComponentReflection();
    Random rng(34);
    const Guid sceneId = Guid::Generate(rng);
    EditorContext context{DefaultAllocator()};
    auto* page = static_cast<HeadlessScenePage*>(context.AdoptPage(UniquePtr<EditorPage>(
        DefaultAllocator().New<HeadlessScenePage>(u8"Bistro", sceneId), DefaultAllocator())));
    SceneEditContext& edit = page->EditContext();
    scene::Scene& scene = edit.Scene();
    auto* plaques = scene.AddSystem<PlaqueManager>();
    auto* meshes = scene.AddSystem<engine::render::MeshComponentManager>();
    const Guid signId = edit.CreateEntity(u8"Sign");
    const Guid postId = edit.CreateEntity(u8"Post");
    const scene::EntityHandle sign = edit.Resolve(signId);
    plaques->Add(sign);
    const Guid meshAsset = Guid::Generate(rng);
    meshes->Add(sign).mesh.id = meshAsset;
    page->ClearDirty();
    edit.Commands().Clear();

    McpServer server;
    RegisterSceneLiveTools(server, context);
    const String pageGuid = GuidText(sceneId);
    const String signGuid = GuidText(signId);
    const auto set = [&](StringView component, StringView property, StringView valueJson)
    {
        return Call(server, u8"component_set",
                    Format(u8"{{\"page\":\"{}\",\"entity\":\"{}\",\"component\":\"{}\",\"property\":\"{}\","
                           u8"\"value\":{}}}",
                           pageGuid.AsView(), signGuid.AsView(), component, property, valueJson)
                        .AsView());
    };

    // A string, by the component's type name (a plain manager has no serialization id).
    Answer got = set(u8"PlaqueComponent", u8"text", u8"\"Open late\"");
    REQUIRE(got.ok);
    CHECK(plaques->Get(sign)->text == u8"Open late");
    CHECK(got.payload.Get(u8"value").AsString() == StringView(u8"Open late"));
    // A vector as three numbers.
    got = set(u8"PlaqueComponent", u8"offset", u8"[1,2,3]");
    REQUIRE(got.ok);
    CHECK(plaques->Get(sign)->offset.z == doctest::Approx(3.0f));
    CHECK(got.payload.Get(u8"value").Count() == 3);
    // null clears a reference.
    got = set(u8"mesh", u8"mesh", u8"null");
    REQUIRE(got.ok);
    CHECK(meshes->Get(sign)->mesh.id.IsNil());
    CHECK(got.payload.Get(u8"value").IsNull());
    CHECK(edit.Commands().CanUndo());
    CHECK(page->IsDirty());
    // A quaternion as four numbers.
    got = set(u8"PlaqueComponent", u8"rotation", u8"[0,0.7071068,0,0.7071068]");
    REQUIRE(got.ok);
    CHECK(plaques->Get(sign)->rotation.y == doctest::Approx(0.7071068f));
    // An entity reference by the entity's guid, then cleared with null.
    got = set(u8"PlaqueComponent", u8"target", Format(u8"\"{}\"", GuidText(postId).AsView()).AsView());
    REQUIRE(got.ok);
    CHECK(plaques->Get(sign)->target.id == postId);
    CHECK(got.payload.Get(u8"value").AsString() == GuidText(postId).AsView());
    got = set(u8"PlaqueComponent", u8"target", u8"null");
    REQUIRE(got.ok);
    CHECK(plaques->Get(sign)->target.IsNil());
    // An enum by number: the enumerator's VALUE (Wild = 5), read back by name.
    got = set(u8"PlaqueComponent", u8"mood", u8"5");
    REQUIRE(got.ok);
    CHECK(plaques->Get(sign)->mood == PlaqueMood::Wild);
    CHECK(got.payload.Get(u8"value").AsString() == StringView(u8"Wild"));

    // Read-only: refused by name before any group opens; the flag is the contract.
    const i64 stackBefore = edit.Commands().UndoIndex();
    got = set(u8"PlaqueComponent", u8"serial", u8"9");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"property 'serial' of 'PlaqueComponent' is read-only"));
    CHECK(plaques->Get(sign)->serial == 7);
    // Wrong shapes for the new leaves name what they take.
    got = set(u8"PlaqueComponent", u8"text", u8"5");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"property 'text' of 'PlaqueComponent' takes a string"));
    got = set(u8"PlaqueComponent", u8"offset", u8"[1,2]");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"property 'offset' of 'PlaqueComponent' takes "));
    got = set(u8"PlaqueComponent", u8"target", u8"\"not-a-guid\"");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"property 'target' is an entity reference"));
    got = set(u8"PlaqueComponent", u8"mood", u8"2"); // no enumerator has the value 2
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"property 'mood' takes one of: Calm, Loud, Wild"));
    CHECK(edit.Commands().UndoIndex() == stackBefore);
    CHECK(plaques->Get(sign)->mood == PlaqueMood::Wild);

    // Seven undos take the seven writes back, newest first.
    edit.Commands().Undo();
    CHECK(plaques->Get(sign)->mood == PlaqueMood::Calm);
    edit.Commands().Undo();
    CHECK(plaques->Get(sign)->target.id == postId);
    edit.Commands().Undo();
    CHECK(plaques->Get(sign)->target.IsNil());
    edit.Commands().Undo();
    CHECK(plaques->Get(sign)->rotation.y == doctest::Approx(0.0f));
    edit.Commands().Undo();
    CHECK(meshes->Get(sign)->mesh.id == meshAsset);
    edit.Commands().Undo();
    CHECK(plaques->Get(sign)->offset.z == doctest::Approx(0.0f));
    edit.Commands().Undo();
    CHECK(plaques->Get(sign)->text == u8"untitled");

    context.ClosePage(page);
}

TEST_CASE("scene-mcp-tools: the viewport camera reads and moves in degrees (position, yaw, pitch, "
          "lookAt wins), and viewport_screenshot waits for the page's capture frame by frame")
{
    Random rng(35);
    const Guid sceneId = Guid::Generate(rng);
    EditorContext context{DefaultAllocator()};
    auto* page = static_cast<HeadlessScenePage*>(context.AdoptPage(UniquePtr<EditorPage>(
        DefaultAllocator().New<HeadlessScenePage>(u8"Bistro", sceneId), DefaultAllocator())));
    auto* headless = static_cast<HeadlessScenePage*>(context.AdoptPage(UniquePtr<EditorPage>(
        DefaultAllocator().New<HeadlessScenePage>(u8"Menu", Guid::Generate(rng)), DefaultAllocator())));
    page->hasViewport = true;
    McpServer server;
    RegisterSceneLiveTools(server, context);
    const String pageGuid = GuidText(sceneId);
    const String pageArg = Format(u8"{{\"page\":\"{}\"}}", pageGuid.AsView());

    // No viewport: every viewport tool refuses by name.
    context.SetActivePage(headless);
    Answer got = Call(server, u8"viewport_camera_get", u8"{}");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"page 'Menu' has no viewport"));
    got = Call(server, u8"viewport_camera_set", u8"{\"yawDegrees\":90}");
    CHECK_FALSE(got.ok);
    got = AnswerOf(Pump(server, u8"viewport_screenshot", u8"{}"));
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"page 'Menu' has no viewport"));

    // The pose reads in degrees from the camera's radians.
    page->camera.position = Float3{1.0f, 2.0f, 3.0f};
    page->camera.yaw = 0.0f;
    page->camera.pitch = 0.0f;
    got = Call(server, u8"viewport_camera_get", pageArg.AsView());
    REQUIRE(got.ok);
    CHECK(got.payload.Get(u8"position").At(2).AsNumber() == doctest::Approx(3.0));
    CHECK(got.payload.Get(u8"yawDegrees").AsNumber() == doctest::Approx(0.0));
    CHECK(got.payload.Get(u8"forward").At(2).AsNumber() == doctest::Approx(-1.0)); // yaw 0 looks down -Z

    // Set: position, then yaw and pitch in degrees; the pitch clamps short of the pole.
    got = Call(server, u8"viewport_camera_set",
               Format(u8"{{\"page\":\"{}\",\"position\":[10,5,0],\"yawDegrees\":90,\"pitchDegrees\":-30}}",
                      pageGuid.AsView())
                   .AsView());
    REQUIRE(got.ok);
    CHECK(page->camera.position.x == doctest::Approx(10.0f));
    CHECK(page->camera.yaw == doctest::Approx(kHalfPi));
    CHECK(page->camera.pitch == doctest::Approx(-30.0f * kPi / 180.0f));
    CHECK(got.payload.Get(u8"yawDegrees").AsNumber() == doctest::Approx(90.0));
    got = Call(server, u8"viewport_camera_set",
               Format(u8"{{\"page\":\"{}\",\"pitchDegrees\":-120}}", pageGuid.AsView()).AsView());
    REQUIRE(got.ok);
    CHECK(got.payload.Get(u8"pitchDegrees").AsNumber() == doctest::Approx(-89.0));
    // lookAt aims from the position and wins over yaw and pitch given beside it.
    got = Call(server, u8"viewport_camera_set",
               Format(u8"{{\"page\":\"{}\",\"position\":[0,0,10],\"yawDegrees\":45,\"lookAt\":[0,0,0]}}",
                      pageGuid.AsView())
                   .AsView());
    REQUIRE(got.ok);
    CHECK(page->camera.yaw == doctest::Approx(0.0f));
    CHECK(page->camera.pitch == doctest::Approx(0.0f));
    CHECK(page->camera.focusDistance == doctest::Approx(10.0f));
    CHECK(got.payload.Get(u8"focusDistance").AsNumber() == doctest::Approx(10.0));
    // Wrong shapes change nothing.
    got = Call(server, u8"viewport_camera_set",
               Format(u8"{{\"page\":\"{}\",\"position\":[1,2],\"yawDegrees\":10}}", pageGuid.AsView()).AsView());
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"`position` takes [x, y, z]"));
    CHECK(page->camera.yaw == doctest::Approx(0.0f));

    // The screenshot: the first pump brings the page to front (active AND revealed, since a
    // background tab's viewport never renders) and asks for the capture, then the call is
    // re-entered each pump until the page reports the frame written.
    context.SetActivePage(headless);
    EditorPage* revealed = nullptr;
    context.OnRevealPage = [&revealed](EditorPage* shown) { revealed = shown; };
    LineOutcome outcome = Pump(server, u8"viewport_screenshot",
                               Format(u8"{{\"page\":\"{}\",\"path\":\"/tmp/bistro.png\"}}", pageGuid.AsView()).AsView());
    CHECK(outcome.state == LineState::NotFinished);
    CHECK(context.ActivePage() == page);
    CHECK(revealed == page);
    CHECK(page->captureRequests == 1u);
    CHECK(page->capture.state == ViewportCaptureState::Pending);
    CHECK(page->capture.path == u8"/tmp/bistro.png");
    outcome = Pump(server, u8"viewport_screenshot",
                   Format(u8"{{\"page\":\"{}\",\"path\":\"/tmp/bistro.png\"}}", pageGuid.AsView()).AsView());
    CHECK(outcome.state == LineState::NotFinished); // not yet rendered
    CHECK(page->captureRequests == 1u);              // the same request, not a new one
    page->CompleteCapture(1280, 720);
    got = AnswerOf(Pump(server, u8"viewport_screenshot",
                        Format(u8"{{\"page\":\"{}\",\"path\":\"/tmp/bistro.png\"}}", pageGuid.AsView()).AsView()));
    REQUIRE(got.ok);
    CHECK(got.payload.Get(u8"path").AsString() == StringView(u8"/tmp/bistro.png"));
    CHECK(got.payload.Get(u8"width").AsNumber() == doctest::Approx(1280));
    CHECK(got.payload.Get(u8"height").AsNumber() == doctest::Approx(720));
    // A failed capture is an error naming the log; a new call starts a new request.
    outcome = Pump(server, u8"viewport_screenshot",
                   Format(u8"{{\"page\":\"{}\",\"path\":\"/tmp/again.png\"}}", pageGuid.AsView()).AsView());
    CHECK(outcome.state == LineState::NotFinished);
    CHECK(page->captureRequests == 2u);
    page->FailCapture();
    got = AnswerOf(Pump(server, u8"viewport_screenshot",
                        Format(u8"{{\"page\":\"{}\",\"path\":\"/tmp/again.png\"}}", pageGuid.AsView()).AsView()));
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().StartsWith(u8"the capture of page 'Bistro' failed"));

    context.ClosePage(page);
    context.ClosePage(headless);
}
