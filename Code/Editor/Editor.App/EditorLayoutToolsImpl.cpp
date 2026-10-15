// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App - the layout tools' bodies.
module;
#include "Core/Prelude.h"

module editor.app;

import foundation.core;
import foundation.json;
import foundation.mcp;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;

using namespace foundation::core;
using foundation::json::JsonValue;
using foundation::mcp::SchemaBuilder;
using foundation::mcp::ToolAnnotations;
using foundation::mcp::ToolResult;
namespace toolkit = foundation::ui::toolkit;

namespace editor::app
{
    namespace
    {
        JsonValue PanelJson(const toolkit::DockablePanel& panel)
        {
            JsonValue out = JsonValue::MakeObject();
            out.Set(u8"panel", JsonValue::MakeString(String(panel.PersistenceId())));
            out.Set(u8"title", JsonValue::MakeString(String(panel.Title())));
            return out;
        }

        // A node of the dock tree: a split with its two sides, or a tab group with its panels.
        JsonValue NodeJson(const foundation::ui::View* node)
        {
            if (const auto* split = Cast<toolkit::DockSplit>(node))
            {
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"split", JsonValue::MakeString(split->Orientation() == foundation::ui::Orientation::Horizontal
                                                             ? u8"horizontal"
                                                             : u8"vertical"));
                out.Set(u8"share", JsonValue::MakeNumber(split->SplitRatio()));
                out.Set(u8"first", NodeJson(split->First()));
                out.Set(u8"second", NodeJson(split->Second()));
                return out;
            }
            if (const auto* group = Cast<toolkit::DockTabGroup>(node))
            {
                JsonValue tabs = JsonValue::MakeArray();
                for (i32 i = 0; i < group->PanelCount(); ++i)
                {
                    tabs.Add(PanelJson(*group->GetPanel(i)));
                }
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"tabs", Move(tabs));
                if (const toolkit::DockablePanel* active = group->SelectedPanel())
                {
                    out.Set(u8"active", JsonValue::MakeString(String(active->PersistenceId())));
                }
                return out;
            }
            if (const auto* panel = Cast<toolkit::DockablePanel>(node))
            {
                JsonValue tabs = JsonValue::MakeArray();
                tabs.Add(PanelJson(*panel));
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"tabs", Move(tabs));
                out.Set(u8"active", JsonValue::MakeString(String(panel->PersistenceId())));
                return out;
            }
            return JsonValue{};
        }

        // In the tree: in a tab group, or the tree's node itself.
        bool IsDocked(const toolkit::DockManager& dock, const toolkit::DockablePanel& panel)
        {
            return Cast<toolkit::DockTabGroup>(panel.Parent) != nullptr || Cast<toolkit::DockSplit>(panel.Parent) != nullptr ||
                   (panel.Parent == &dock && dock.RootNode() == &panel);
        }

        JsonValue LayoutJson(toolkit::DockManager& dock)
        {
            JsonValue floating = JsonValue::MakeArray();
            JsonValue hidden = JsonValue::MakeArray();
            for (i32 i = 0; i < dock.RegisteredPanelCount(); ++i)
            {
                const toolkit::DockablePanel* panel = dock.RegisteredPanelAt(i);
                if (panel == nullptr || panel->IsPendingDeletion || IsDocked(dock, *panel))
                {
                    continue;
                }
                if (Cast<toolkit::DockableWindow>(panel->Parent) != nullptr)
                {
                    floating.Add(PanelJson(*panel));
                }
                else
                {
                    hidden.Add(PanelJson(*panel)); // a tool panel closed for now
                }
            }
            JsonValue out = JsonValue::MakeObject();
            out.Set(u8"root", NodeJson(dock.RootNode()));
            out.Set(u8"floating", Move(floating));
            out.Set(u8"hidden", Move(hidden));
            return out;
        }

        Result<toolkit::DockPosition, String> ParsePosition(StringView text)
        {
            if (text == u8"left")
            {
                return toolkit::DockPosition::Left;
            }
            if (text == u8"right")
            {
                return toolkit::DockPosition::Right;
            }
            if (text == u8"top")
            {
                return toolkit::DockPosition::Top;
            }
            if (text == u8"bottom")
            {
                return toolkit::DockPosition::Bottom;
            }
            if (text == u8"center")
            {
                return toolkit::DockPosition::Center;
            }
            if (text == u8"float")
            {
                return toolkit::DockPosition::Float;
            }
            return Err(Format(u8"unknown position '{}' (left, right, top, bottom, center or float)", text));
        }

        Array<String> PositionChoices()
        {
            static constexpr StringView kNames[] = {u8"left", u8"right", u8"top", u8"bottom", u8"center", u8"float"};
            Array<String> choices(editor::EditorRootAllocator());
            for (StringView name : kNames)
            {
                choices.PushBack(String(name));
            }
            return choices;
        }

        toolkit::DockablePanel* FindLivePanel(toolkit::DockManager& dock, StringView id)
        {
            toolkit::DockablePanel* panel = id.IsEmpty() ? nullptr : dock.FindPanelById(id);
            return panel != nullptr && !panel->IsPendingDeletion ? panel : nullptr;
        }
    }

    void RegisterLayoutTools(foundation::mcp::McpServer& server, toolkit::DockManager& dock)
    {
        toolkit::DockManager* docks = &dock;

        server.RegisterTool(
            u8"layout_get",
            u8"The editor's dock layout: `root`, a tree whose nodes are splits ({split: horizontal "
            u8"(first left, second right) or vertical (first above), share: the first side's "
            u8"fraction, first, second}) or tab groups ({tabs: [{panel, title}], active}), and the "
            u8"`floating` and `hidden` (closed tool) panels. A panel is named by its id: a page's "
            u8"asset guid, a Game tab's PIE id (game-page, game-page-1, ...), or a tool panel's "
            u8"(welcome is Project; assets, console, profiler). panel_dock moves them.",
            SchemaBuilder().Build(), ToolAnnotations::ReadOnly(),
            [docks](const JsonValue&) -> ToolResult { return LayoutJson(*docks); });

        server.RegisterTool(
            u8"panel_dock",
            u8"Move a panel, as dragging its tab does: to the `left`, `right`, `top` or `bottom` of "
            u8"another panel's tab group (`relativeTo`; default: the whole window), into that group "
            u8"as a tab (`center`), or into a window of its own (`float`). A hidden tool panel docks "
            u8"again the same way. `share` (0.05 to 0.95, default 0.5) is the moved panel's "
            u8"fraction of the split a side position makes. Two Game tabs side by side for a "
            u8"networked playtest: {panel: game-page-1, position: right, relativeTo: game-page}. "
            u8"Returns the layout as layout_get gives it. Ids come from layout_get.",
            SchemaBuilder()
                .Str(u8"panel", u8"the panel to move, by its id", true)
                .Enum(u8"position", PositionChoices(), u8"where it goes", true)
                .Str(u8"relativeTo", u8"the panel whose tab group it goes beside or into (default: the window)")
                .Number(u8"share", u8"the moved panel's fraction of the split (side positions; default 0.5)")
                .Build(),
            ToolAnnotations::Adjusts(),
            [docks](const JsonValue& args) -> ToolResult
            {
                const String id = args.Get(u8"panel").AsString();
                toolkit::DockablePanel* panel = FindLivePanel(*docks, id.AsView());
                if (panel == nullptr)
                {
                    return Err(Format(u8"no panel '{}' (layout_get lists them)", id.AsView()));
                }
                Result<toolkit::DockPosition, String> position = ParsePosition(args.Get(u8"position").AsString().AsView());
                if (!position.HasValue())
                {
                    return Err(Move(position.Error()));
                }
                const bool side = position.Value() != toolkit::DockPosition::Center &&
                                  position.Value() != toolkit::DockPosition::Float;
                const bool hasShare = args.Get(u8"share").IsNumber();
                const f32 share = static_cast<f32>(args.Get(u8"share").AsNumber(0.5));
                if (hasShare && (!side || share < 0.05f || share > 0.95f))
                {
                    return Err(String(u8"share is a side position's fraction, 0.05 to 0.95"));
                }
                const String relativeId = args.Get(u8"relativeTo").AsString();
                toolkit::DockablePanel* relativeTo = nullptr;
                if (!relativeId.IsEmpty())
                {
                    relativeTo = FindLivePanel(*docks, relativeId.AsView());
                    if (relativeTo == nullptr)
                    {
                        return Err(Format(u8"no panel '{}' (layout_get lists them)", relativeId.AsView()));
                    }
                    if (relativeTo == panel)
                    {
                        return Err(String(u8"a panel cannot go beside or into itself"));
                    }
                    if (!IsDocked(*docks, *relativeTo))
                    {
                        return Err(Format(u8"panel '{}' is not docked (it floats or is hidden); dock it first",
                                          relativeId.AsView()));
                    }
                }
                if (relativeTo != nullptr)
                {
                    docks->DockPanelRelativeTo(panel, position.Value(), relativeTo);
                }
                else
                {
                    docks->DockPanel(panel, position.Value());
                }
                if (side)
                {
                    // The new split holds the panel's own group on one side.
                    foundation::ui::View* group = panel->Parent;
                    if (auto* split = group != nullptr ? Cast<toolkit::DockSplit>(group->Parent) : nullptr)
                    {
                        split->SetSplitRatio(split->First() == group ? share : 1.0f - share);
                    }
                }
                docks->ActivatePanel(panel);
                return LayoutJson(*docks);
            });
    }
}
