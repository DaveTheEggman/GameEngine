// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::Scene - the scene editor's MCP tools' bodies.
module;
#include "Core/Prelude.h"

module editor.scene;

import foundation.core;
import foundation.json;
import foundation.mcp;
import foundation.scene;
import editor.core;

using namespace foundation::core;
using foundation::json::JsonValue;
using foundation::mcp::SchemaBuilder;
using foundation::mcp::ToolAnnotations;
using foundation::mcp::ToolResult;
namespace scene = foundation::scene;

namespace editor
{
    namespace
    {
        JsonValue GuidJson(const Guid& id)
        {
            utf8char text[37];
            id.ToChars(text);
            return JsonValue::MakeString(StringView(text, 36));
        }

        /// The scene page a call addresses: `page` (a guid) when given, else the active page -
        /// and the reason when it is not a scene page, for the agent to read.
        struct AddressedPage
        {
            EditorPage* page = nullptr;
            ISceneEditorPage* scene = nullptr;
        };
        Result<AddressedPage, String> ResolveScenePage(EditorContext& context, const JsonValue& args)
        {
            AddressedPage addressed;
            const JsonValue pageArg = args.Get(u8"page");
            if (pageArg.IsString())
            {
                const String text = pageArg.AsString();
                Guid id;
                if (!Guid::TryParse(text.AsView(), id))
                {
                    return Err(Format(u8"invalid page guid '{}'", text.AsView()));
                }
                for (const UniquePtr<EditorPage>& open : context.OpenPages())
                {
                    if (open->InstanceId() == id)
                    {
                        addressed.page = open.Get();
                        break;
                    }
                }
                if (addressed.page == nullptr)
                {
                    return Err(Format(u8"no open page for guid '{}' (page_list shows the open ones; "
                                      u8"page_open opens one)",
                                      text.AsView()));
                }
            }
            else
            {
                addressed.page = context.ActivePage();
                if (addressed.page == nullptr)
                {
                    return Err(String(u8"no page is active - page_open a scene first, or pass "
                                      u8"`page`"));
                }
            }
            addressed.scene = addressed.page->Service<ISceneEditorPage>();
            if (addressed.scene == nullptr)
            {
                return Err(Format(u8"page '{}' is not a scene or prefab page - pass `page` with a "
                                  u8"scene's guid, or page_open one",
                                  addressed.page->Title()));
            }
            return addressed;
        }

        JsonValue PageJson(const EditorPage& page)
        {
            JsonValue out = JsonValue::MakeObject();
            out.Set(u8"guid", GuidJson(page.InstanceId()));
            out.Set(u8"title", JsonValue::MakeString(String(page.Title())));
            return out;
        }

        /// The page's selection as the agent sees it: the page, the entities in order (the first
        /// is the primary, the gizmo pivot), each with its name.
        JsonValue SelectionJson(const AddressedPage& addressed)
        {
            SceneEditContext& edit = addressed.scene->EditContext();
            JsonValue entities = JsonValue::MakeArray();
            for (const Guid& id : edit.EntitySelection().Items())
            {
                JsonValue entry = JsonValue::MakeObject();
                entry.Set(u8"guid", GuidJson(id));
                const scene::EntityHandle handle = edit.Resolve(id);
                entry.Set(u8"name", JsonValue::MakeString(String(
                                        edit.Scene().IsValid(handle)
                                            ? edit.Scene().GetEntityName(handle)
                                            : StringView())));
                entities.Add(Move(entry));
            }
            JsonValue out = JsonValue::MakeObject();
            out.Set(u8"page", PageJson(*addressed.page));
            const Guid* primary = edit.EntitySelection().Primary();
            out.Set(u8"primary", primary != nullptr ? GuidJson(*primary) : JsonValue::MakeNull());
            out.Set(u8"entities", Move(entities));
            return out;
        }

        JsonValue SimulationJson(const AddressedPage& addressed)
        {
            JsonValue out = JsonValue::MakeObject();
            out.Set(u8"page", PageJson(*addressed.page));
            out.Set(u8"simulating", JsonValue::MakeBool(addressed.scene->IsSimulating()));
            return out;
        }

        constexpr StringView kPageArgument =
            u8"the scene or prefab page's asset guid (default: the active page)";
    }

    void RegisterSceneLiveTools(foundation::mcp::McpServer& server, EditorContext& context)
    {
        EditorContext* ctx = &context;

        server.RegisterTool(
            u8"selection_get",
            u8"A scene page's entity selection: the entities in order (the first is the primary - "
            u8"the gizmo pivot), each with its name. Defaults to the active page.",
            SchemaBuilder().Str(u8"page", kPageArgument).Build(), ToolAnnotations::ReadOnly(),
            [ctx](const JsonValue& args) -> ToolResult
            {
                Result<AddressedPage, String> addressed = ResolveScenePage(*ctx, args);
                if (!addressed.HasValue())
                {
                    return Err(Move(addressed.Error()));
                }
                return SelectionJson(addressed.Value());
            });

        server.RegisterTool(
            u8"selection_set",
            u8"Select entities on a scene page (an empty list clears): the hierarchy, the "
            u8"inspector and the gizmos follow, so this is also how to SHOW the user which entity "
            u8"is meant. The first guid becomes the primary. Every guid must be an entity of that "
            u8"page's scene. Returns the selection as selection_get does.",
            SchemaBuilder()
                .Str(u8"page", kPageArgument)
                .Arr(u8"entities", u8"string", u8"the entity guids to select, in order", true)
                .Build(),
            ToolAnnotations::Adjusts(),
            [ctx](const JsonValue& args) -> ToolResult
            {
                Result<AddressedPage, String> addressed = ResolveScenePage(*ctx, args);
                if (!addressed.HasValue())
                {
                    return Err(Move(addressed.Error()));
                }
                SceneEditContext& edit = addressed.Value().scene->EditContext();
                const JsonValue list = args.Get(u8"entities");
                Array<Guid> ids;
                for (usize i = 0; i < static_cast<usize>(list.Count()); ++i)
                {
                    const String text = list.At(i).AsString();
                    Guid id;
                    if (!Guid::TryParse(text.AsView(), id))
                    {
                        return Err(Format(u8"invalid entity guid '{}'", text.AsView()));
                    }
                    if (!edit.Scene().IsValid(edit.Resolve(id)))
                    {
                        return Err(Format(u8"no entity with guid '{}' in page '{}' (scene_read shows "
                                          u8"the scene's entities)",
                                          text.AsView(), addressed.Value().page->Title()));
                    }
                    ids.PushBack(id);
                }
                edit.EntitySelection().Set(Span<const Guid>{ids.Data(), ids.Size()});
                return SelectionJson(addressed.Value());
            });

        server.RegisterTool(
            u8"simulate_start",
            u8"Start a scene page's edit-mode Simulate: the live scene runs (physics, systems) from "
            u8"a snapshot that simulate_stop restores; edits are locked meanwhile. A no-op when "
            u8"already simulating. Returns {page, simulating}.",
            SchemaBuilder().Str(u8"page", kPageArgument).Build(), ToolAnnotations::Adjusts(),
            [ctx](const JsonValue& args) -> ToolResult
            {
                Result<AddressedPage, String> addressed = ResolveScenePage(*ctx, args);
                if (!addressed.HasValue())
                {
                    return Err(Move(addressed.Error()));
                }
                addressed.Value().scene->StartSimulation();
                return SimulationJson(addressed.Value());
            });

        server.RegisterTool(
            u8"simulate_stop",
            u8"Stop a scene page's Simulate and restore the scene from its snapshot. A no-op when "
            u8"not simulating. Returns {page, simulating}.",
            SchemaBuilder().Str(u8"page", kPageArgument).Build(), ToolAnnotations::Adjusts(),
            [ctx](const JsonValue& args) -> ToolResult
            {
                Result<AddressedPage, String> addressed = ResolveScenePage(*ctx, args);
                if (!addressed.HasValue())
                {
                    return Err(Move(addressed.Error()));
                }
                addressed.Value().scene->StopSimulation();
                return SimulationJson(addressed.Value());
            });
    }
}
