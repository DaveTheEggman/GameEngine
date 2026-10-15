// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App tests - the layout tools over a headless editor shell's dock: layout_get's tree of
// splits and tab groups, panel_dock putting one Game tab beside another (the networked playtest's
// layout), a side's share, a tab back into a group, a hidden tool panel docked again, and the
// refusals.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.json;
import foundation.mcp;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;
import editor.app;

using namespace foundation::core;
using namespace foundation::mcp;
using namespace editor;
using namespace editor::app;
namespace json = foundation::json;
namespace ui = foundation::ui;
using json::JsonValue;

namespace
{
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

    bool HoldsTab(const JsonValue& group, StringView panel)
    {
        const JsonValue tabs = group.Get(u8"tabs");
        for (i64 i = 0; i < tabs.Count(); ++i)
        {
            if (tabs.At(i).Get(u8"panel").AsString() == panel)
            {
                return true;
            }
        }
        return false;
    }

    // The tab group of a layout_get tree that holds `panel`, named by its tabs' ids ("a,b");
    // empty when no group holds it. Two panels share a group when their names match.
    String GroupOf(const JsonValue& node, StringView panel)
    {
        if (node.Has(String(u8"tabs")))
        {
            if (!HoldsTab(node, panel))
            {
                return String();
            }
            String name;
            const JsonValue tabs = node.Get(u8"tabs");
            for (i64 i = 0; i < tabs.Count(); ++i)
            {
                name += Format(u8"{}{}", i == 0 ? u8"" : u8",", tabs.At(i).Get(u8"panel").AsString().AsView()).AsView();
            }
            return name;
        }
        if (node.Has(String(u8"split")))
        {
            String found = GroupOf(node.Get(u8"first"), panel);
            return found.IsEmpty() ? GroupOf(node.Get(u8"second"), panel) : found;
        }
        return String();
    }

    // The split whose first side is the group holding `first` and whose second is the group
    // holding `second`; null when there is none.
    JsonValue SplitOver(const JsonValue& node, StringView first, StringView second)
    {
        if (!node.Has(String(u8"split")))
        {
            return JsonValue{};
        }
        const JsonValue a = node.Get(u8"first");
        const JsonValue b = node.Get(u8"second");
        if (a.Has(String(u8"tabs")) && b.Has(String(u8"tabs")) && HoldsTab(a, first) && HoldsTab(b, second))
        {
            return node;
        }
        JsonValue found = SplitOver(a, first, second);
        return found.IsNull() ? SplitOver(b, first, second) : found;
    }

    bool Lists(const JsonValue& panels, StringView id)
    {
        for (i64 i = 0; i < panels.Count(); ++i)
        {
            if (panels.At(i).Get(u8"panel").AsString() == id)
            {
                return true;
            }
        }
        return false;
    }

    // A headless shell with two Game tabs among the documents, as Play and Play New Instance
    // leave them.
    struct LayoutRig
    {
        EditorContext context{DefaultAllocator()};
        EditorShell shell;
        McpServer server;
        RefPtr<ui::Label> hostView;
        RefPtr<ui::Label> clientView;

        LayoutRig()
        {
            shell.Build(context, nullptr, 1280, 720);
            hostView = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"host"));
            clientView = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"client"));
            shell.AddPagePanel(u8"Game", hostView.Get())->SetPersistenceId(u8"game-page");
            shell.AddPagePanel(u8"Game", clientView.Get())->SetPersistenceId(u8"game-page-1");
            RegisterLayoutTools(server, *shell.Docks());
        }
    };
}

TEST_CASE("layout-tools: two Game tabs go side by side, then back into one group")
{
    LayoutRig rig;
    CHECK(rig.server.ToolCount() == kLayoutToolCount);

    // Both tabs in the document group, with the Project panel.
    Answer got = Call(rig.server, u8"layout_get", u8"{}");
    REQUIRE(got.ok);
    const JsonValue root = got.payload.Get(u8"root");
    REQUIRE_FALSE(GroupOf(root, u8"game-page").IsEmpty());
    CHECK(GroupOf(root, u8"game-page") == GroupOf(root, u8"game-page-1"));
    CHECK(GroupOf(root, u8"welcome") == GroupOf(root, u8"game-page"));
    CHECK_FALSE(GroupOf(root, u8"console").IsEmpty());

    // The client to the right of the host: a horizontal split, the host's group left, the
    // client alone right, half each.
    got = Call(rig.server, u8"panel_dock",
               u8"{\"panel\":\"game-page-1\",\"position\":\"right\",\"relativeTo\":\"game-page\"}");
    REQUIRE(got.ok);
    {
        const JsonValue tree = got.payload.Get(u8"root");
        CHECK(GroupOf(tree, u8"game-page-1") == u8"game-page-1"); // alone in its group
        CHECK(GroupOf(tree, u8"welcome") == GroupOf(tree, u8"game-page")); // the rest stay with the host
        const JsonValue split = SplitOver(tree, u8"game-page", u8"game-page-1");
        REQUIRE_FALSE(split.IsNull());
        CHECK(split.Get(u8"split").AsString() == u8"horizontal");
        CHECK(split.Get(u8"share").AsNumber() == doctest::Approx(0.5));
    }
    // The dock agrees: both are their groups' selected tabs, so both render.
    auto* hostPanel = rig.shell.Docks()->FindPanelById(u8"game-page");
    auto* clientPanel = rig.shell.Docks()->FindPanelById(u8"game-page-1");
    CHECK(Cast<ui::toolkit::DockTabGroup>(clientPanel->Parent)->SelectedPanel() == clientPanel);
    CHECK(hostPanel->Parent != clientPanel->Parent);

    // Back into the host's group as a tab: the split goes.
    got = Call(rig.server, u8"panel_dock",
               u8"{\"panel\":\"game-page-1\",\"position\":\"center\",\"relativeTo\":\"game-page\"}");
    REQUIRE(got.ok);
    CHECK(GroupOf(got.payload.Get(u8"root"), u8"game-page") == GroupOf(got.payload.Get(u8"root"), u8"game-page-1"));
    CHECK(hostPanel->Parent == clientPanel->Parent);
}

TEST_CASE("layout-tools: a side's share is the moved panel's, on either side")
{
    LayoutRig rig;
    // Below the host, a third of the height: the host's group is first, so its share is 2/3.
    Answer got = Call(rig.server, u8"panel_dock",
                      u8"{\"panel\":\"game-page-1\",\"position\":\"bottom\",\"relativeTo\":\"game-page\",\"share\":0.3}");
    REQUIRE(got.ok);
    JsonValue split = SplitOver(got.payload.Get(u8"root"), u8"game-page", u8"game-page-1");
    REQUIRE_FALSE(split.IsNull());
    CHECK(split.Get(u8"split").AsString() == u8"vertical");
    CHECK(split.Get(u8"share").AsNumber() == doctest::Approx(0.7));

    // Left of the host, a quarter: now the moved panel is first.
    got = Call(rig.server, u8"panel_dock",
               u8"{\"panel\":\"game-page-1\",\"position\":\"left\",\"relativeTo\":\"game-page\",\"share\":0.25}");
    REQUIRE(got.ok);
    split = SplitOver(got.payload.Get(u8"root"), u8"game-page-1", u8"game-page");
    REQUIRE_FALSE(split.IsNull());
    CHECK(split.Get(u8"share").AsNumber() == doctest::Approx(0.25));
}

TEST_CASE("layout-tools: a hidden tool panel is listed, and docks again")
{
    LayoutRig rig;
    rig.shell.Docks()->ClosePanel(rig.shell.ProfilerTab()); // a tool panel hides on close
    Answer got = Call(rig.server, u8"layout_get", u8"{}");
    REQUIRE(got.ok);
    CHECK(Lists(got.payload.Get(u8"hidden"), u8"profiler"));
    CHECK(GroupOf(got.payload.Get(u8"root"), u8"profiler").IsEmpty());

    got = Call(rig.server, u8"panel_dock", u8"{\"panel\":\"profiler\",\"position\":\"center\",\"relativeTo\":\"console\"}");
    REQUIRE(got.ok);
    CHECK_FALSE(Lists(got.payload.Get(u8"hidden"), u8"profiler"));
    CHECK(GroupOf(got.payload.Get(u8"root"), u8"profiler") == GroupOf(got.payload.Get(u8"root"), u8"console"));
}

TEST_CASE("layout-tools: refusals name what is wrong")
{
    LayoutRig rig;
    Answer got = Call(rig.server, u8"panel_dock", u8"{\"panel\":\"game-page-9\",\"position\":\"right\"}");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().ContainsIgnoreCase(u8"layout_get"));

    got = Call(rig.server, u8"panel_dock",
               u8"{\"panel\":\"game-page\",\"position\":\"right\",\"relativeTo\":\"game-page\"}");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().ContainsIgnoreCase(u8"itself"));

    got = Call(rig.server, u8"panel_dock",
               u8"{\"panel\":\"game-page-1\",\"position\":\"center\",\"relativeTo\":\"game-page\",\"share\":0.3}");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().ContainsIgnoreCase(u8"share"));

    rig.shell.Docks()->ClosePanel(rig.shell.ProfilerTab());
    got = Call(rig.server, u8"panel_dock",
               u8"{\"panel\":\"game-page-1\",\"position\":\"right\",\"relativeTo\":\"profiler\"}");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().ContainsIgnoreCase(u8"not docked"));

    // Nothing moved.
    got = Call(rig.server, u8"layout_get", u8"{}");
    CHECK(GroupOf(got.payload.Get(u8"root"), u8"game-page") == GroupOf(got.payload.Get(u8"root"), u8"game-page-1"));
}
