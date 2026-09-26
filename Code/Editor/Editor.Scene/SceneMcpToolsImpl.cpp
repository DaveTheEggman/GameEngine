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
import foundation.scene.resource;
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


        // === entity_inspect: the entity and its components as reflection reads them ===

        JsonValue Float3Json(const Float3& v)
        {
            JsonValue out = JsonValue::MakeArray();
            out.Add(JsonValue::MakeNumber(v.x));
            out.Add(JsonValue::MakeNumber(v.y));
            out.Add(JsonValue::MakeNumber(v.z));
            return out;
        }
        JsonValue QuadJson(f32 a, f32 b, f32 c, f32 d)
        {
            JsonValue out = JsonValue::MakeArray();
            out.Add(JsonValue::MakeNumber(a));
            out.Add(JsonValue::MakeNumber(b));
            out.Add(JsonValue::MakeNumber(c));
            out.Add(JsonValue::MakeNumber(d));
            return out;
        }

        /// A leaf value as JSON: what the Variant's type says it is. A reference-shaped value
        /// (a resource ref) is its id, an enum its enumerator's name, an entity ref its guid.
        JsonValue ValueJson(const Variant& value)
        {
            if (value.IsEmpty() || value.Type() == nullptr)
            {
                return JsonValue::MakeNull();
            }
            const TypeInfo& type = *value.Type();
            if (IsReferenceType(type))
            {
                const Guid* id = type.referenceId(value.ValuePointer());
                return (id != nullptr && !id->IsNil()) ? GuidJson(*id) : JsonValue::MakeNull();
            }
            if (type.enumeratorCount > 0)
            {
                const i64 raw = value.AsEnumInt();
                for (u32 i = 0; i < type.enumeratorCount; ++i)
                {
                    if (type.enumerators[i].value == raw)
                    {
                        return JsonValue::MakeString(StringView(
                            reinterpret_cast<const utf8char*>(type.enumerators[i].name)));
                    }
                }
                return JsonValue::MakeNumber(static_cast<f64>(raw));
            }
            if (value.Is<bool>())
            {
                return JsonValue::MakeBool(value.Get<bool>());
            }
            if (value.Is<f32>())
            {
                return JsonValue::MakeNumber(value.Get<f32>());
            }
            if (value.Is<f64>())
            {
                return JsonValue::MakeNumber(value.Get<f64>());
            }
            if (value.Is<i32>())
            {
                return JsonValue::MakeNumber(value.Get<i32>());
            }
            if (value.Is<u32>())
            {
                return JsonValue::MakeNumber(value.Get<u32>());
            }
            if (value.Is<i64>())
            {
                return JsonValue::MakeNumber(static_cast<f64>(value.Get<i64>()));
            }
            if (value.Is<u64>())
            {
                return JsonValue::MakeNumber(static_cast<f64>(value.Get<u64>()));
            }
            if (value.Is<i16>())
            {
                return JsonValue::MakeNumber(value.Get<i16>());
            }
            if (value.Is<u16>())
            {
                return JsonValue::MakeNumber(value.Get<u16>());
            }
            if (value.Is<i8>())
            {
                return JsonValue::MakeNumber(value.Get<i8>());
            }
            if (value.Is<u8>())
            {
                return JsonValue::MakeNumber(value.Get<u8>());
            }
            if (value.Is<String>())
            {
                return JsonValue::MakeString(value.Get<String>());
            }
            if (value.Is<Guid>())
            {
                return GuidJson(value.Get<Guid>());
            }
            if (value.Is<scene::EntityRef>())
            {
                const Guid& id = value.Get<scene::EntityRef>().id;
                return id.IsNil() ? JsonValue::MakeNull() : GuidJson(id);
            }
            if (value.Is<Float2>())
            {
                const Float2& v = value.Get<Float2>();
                JsonValue out = JsonValue::MakeArray();
                out.Add(JsonValue::MakeNumber(v.x));
                out.Add(JsonValue::MakeNumber(v.y));
                return out;
            }
            if (value.Is<Float3>())
            {
                return Float3Json(value.Get<Float3>());
            }
            if (value.Is<Float4>())
            {
                const Float4& v = value.Get<Float4>();
                return QuadJson(v.x, v.y, v.z, v.w);
            }
            if (value.Is<Quaternion>())
            {
                const Quaternion& q = value.Get<Quaternion>();
                return QuadJson(q.x, q.y, q.z, q.w);
            }
            if (value.Is<Color>())
            {
                const Color& c = value.Get<Color>();
                return QuadJson(c.r, c.g, c.b, c.a);
            }
            // A value type this reader has no spelling for: its type name, so the agent knows
            // there is something here it cannot read yet.
            JsonValue unknown = JsonValue::MakeObject();
            unknown.Set(u8"unreadable", JsonValue::MakeString(StringView(
                                            reinterpret_cast<const utf8char*>(type.name))));
            return unknown;
        }

        JsonValue PropertiesJson(const TypeInfo& type, const Instance& instance);

        /// One property: a leaf through its Variant; a nested structure recursed into; a
        /// container as an array of its elements (each a leaf or a structure).
        JsonValue PropertyJson(const PropertyInfo& property, const Instance& instance)
        {
            if (!IsNested(property))
            {
                return ValueJson(GetProperty(property, instance));
            }
            void* address = property.address != nullptr ? property.address(instance) : nullptr;
            if (address == nullptr || property.type == nullptr)
            {
                return JsonValue::MakeNull();
            }
            const Instance nested(address, property.type);
            if (property.type->container != nullptr)
            {
                const ContainerInfo& container = *property.type->container;
                JsonValue items = JsonValue::MakeArray();
                const usize count = ContainerSize(container, nested);
                for (usize i = 0; i < count; ++i)
                {
                    const Variant element = ContainerGetAt(container, nested, i);
                    if (!element.IsEmpty() && element.Type() != nullptr &&
                        element.Type()->propertyCount > 0 && !IsReferenceType(*element.Type()))
                    {
                        items.Add(PropertiesJson(
                            *element.Type(),
                            Instance(const_cast<void*>(element.ValuePointer()), element.Type())));
                    }
                    else
                    {
                        items.Add(ValueJson(element));
                    }
                }
                return items;
            }
            return PropertiesJson(*property.type, nested);
        }

        /// A reflected structure's properties, by name, in declaration order.
        JsonValue PropertiesJson(const TypeInfo& type, const Instance& instance)
        {
            JsonValue out = JsonValue::MakeObject();
            for (const PropertyInfo& property : Properties(type))
            {
                out.Set(StringView(reinterpret_cast<const utf8char*>(property.name)),
                        PropertyJson(property, instance));
            }
            return out;
        }

        /// The entity as the agent sees it: identity, hierarchy, transform, and every
        /// component the scene holds for it with its reflected properties.
        JsonValue EntityJson(SceneEditContext& edit, const Guid& id)
        {
            scene::Scene& scene = edit.Scene();
            const scene::EntityHandle handle = edit.Resolve(id);
            JsonValue out = JsonValue::MakeObject();
            out.Set(u8"guid", GuidJson(id));
            out.Set(u8"name", JsonValue::MakeString(String(scene.GetEntityName(handle))));
            out.Set(u8"active", JsonValue::MakeBool(scene.IsActive(handle)));
            const scene::EntityHandle parent = scene.GetParent(handle);
            out.Set(u8"parent", parent.IsAssigned() ? GuidJson(scene.GetEntityId(parent))
                                                    : JsonValue::MakeNull());
            JsonValue children = JsonValue::MakeArray();
            for (scene::EntityHandle child = scene.GetFirstChild(handle); child.IsAssigned();
                 child = scene.GetNextSibling(child))
            {
                children.Add(GuidJson(scene.GetEntityId(child)));
            }
            out.Set(u8"children", Move(children));
            const Transform local = scene.GetLocalTransform(handle);
            JsonValue transform = JsonValue::MakeObject();
            transform.Set(u8"position", Float3Json(local.position));
            transform.Set(u8"rotation", QuadJson(local.rotation.x, local.rotation.y,
                                                 local.rotation.z, local.rotation.w));
            transform.Set(u8"scale", Float3Json(local.scale));
            out.Set(u8"transform", Move(transform));
            JsonValue components = JsonValue::MakeArray();
            scene.ForEachManager(
                [&](scene::ComponentManagerBase& manager)
                {
                    if (!manager.HasComponent(handle))
                    {
                        return;
                    }
                    JsonValue component = JsonValue::MakeObject();
                    component.Set(u8"type",
                                  JsonValue::MakeString(String(manager.SerializationTypeId())));
                    const TypeInfo* type = manager.ComponentType();
                    const Instance instance = manager.GetComponentInstance(handle);
                    if (type != nullptr && instance.Pointer() != nullptr)
                    {
                        component.Set(u8"typeName",
                                      JsonValue::MakeString(StringView(
                                          reinterpret_cast<const utf8char*>(type->name))));
                        component.Set(u8"properties", PropertiesJson(*type, instance));
                    }
                    else
                    {
                        component.Set(u8"properties", JsonValue::MakeNull()); // unreflected
                    }
                    components.Add(Move(component));
                });
            out.Set(u8"components", Move(components));
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

        server.RegisterTool(
            u8"entity_inspect",
            u8"An entity of a scene page as the editor's inspector sees it: guid, name, active, "
            u8"parent, children, the local transform, and every component the scene holds for it "
            u8"with its reflected properties (references as asset guids, enums by name, nested "
            u8"structures and lists expanded). Defaults to the page's primary selection.",
            SchemaBuilder()
                .Str(u8"page", kPageArgument)
                .Str(u8"entity", u8"the entity's guid (default: the page's primary selection)")
                .Build(),
            ToolAnnotations::ReadOnly(),
            [ctx](const JsonValue& args) -> ToolResult
            {
                Result<AddressedPage, String> addressed = ResolveScenePage(*ctx, args);
                if (!addressed.HasValue())
                {
                    return Err(Move(addressed.Error()));
                }
                SceneEditContext& edit = addressed.Value().scene->EditContext();
                Guid id;
                const JsonValue entityArg = args.Get(u8"entity");
                if (entityArg.IsString())
                {
                    const String text = entityArg.AsString();
                    if (!Guid::TryParse(text.AsView(), id))
                    {
                        return Err(Format(u8"invalid entity guid '{}'", text.AsView()));
                    }
                }
                else
                {
                    const Guid* primary = edit.EntitySelection().Primary();
                    if (primary == nullptr)
                    {
                        return Err(Format(u8"page '{}' has no selection - pass `entity`, or "
                                          u8"selection_set one first",
                                          addressed.Value().page->Title()));
                    }
                    id = *primary;
                }
                if (!edit.Scene().IsValid(edit.Resolve(id)))
                {
                    utf8char text[37];
                    id.ToChars(text);
                    return Err(Format(u8"no entity with guid '{}' in page '{}' (scene_read shows "
                                      u8"the scene's entities)",
                                      StringView(text, 36), addressed.Value().page->Title()));
                }
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"page", PageJson(*addressed.Value().page));
                out.Set(u8"entity", EntityJson(edit, id));
                return out;
            });
    }
}
