// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::Scene tests - the scene editor's MCP tools over a real EditorContext holding pages
// that publish ISceneEditorPage (a headless scene + edit context behind each) beside one that
// does not: page addressing (the active page, an explicit page, a page that is not a scene),
// the selection round-trip with names and the primary, the refusals for unknown entities and
// pages, and the simulate control reflecting the page's state.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.json;
import foundation.mcp;
import foundation.scene;
import editor.core;
import editor.scene;

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
              m_scene(DefaultAllocator(), u8"headless"), m_edit(m_scene, m_commands)
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

    private:
        String m_title;
        scene::Scene m_scene;
        EditorCommandStack m_commands;
        SceneEditContext m_edit;
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
