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
import editor.camera;

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
                const Guid* id = type.reference->Id(value.ValuePointer());
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


        // === component_set: one property, one locked undo step, through the edit context ===

        /// The manager holding `component` for the entity: by serialization id ("light",
        /// "physics.RigidBody") or by the reflected type's name ("LightComponent").
        scene::ComponentManagerBase* FindComponentManager(scene::Scene& scene,
                                                          scene::EntityHandle entity,
                                                          StringView component)
        {
            scene::ComponentManagerBase* found = nullptr;
            scene.ForEachManager(
                [&](scene::ComponentManagerBase& manager)
                {
                    if (found != nullptr || !manager.HasComponent(entity))
                    {
                        return;
                    }
                    const TypeInfo* type = manager.ComponentType();
                    if (manager.SerializationTypeId() == component ||
                        (type != nullptr &&
                         StringView(reinterpret_cast<const utf8char*>(type->name)) == component))
                    {
                        found = &manager;
                    }
                });
            return found;
        }

        /// A JSON value as the Variant a leaf property of `type` takes; empty when the JSON
        /// has the wrong shape (the refusal names the type).
        Variant LeafVariant(const TypeInfo& type, const JsonValue& value)
        {
            const auto number = [&](auto make) -> Variant
            { return value.IsNumber() ? make(value.AsNumber()) : Variant{}; };
            if (&type == &TypeOf<bool>())
            {
                return value.IsBool() ? Variant::From<bool>(value.AsBool()) : Variant{};
            }
            if (&type == &TypeOf<f32>())
            {
                return number([](f64 n) { return Variant::From<f32>(static_cast<f32>(n)); });
            }
            if (&type == &TypeOf<f64>())
            {
                return number([](f64 n) { return Variant::From<f64>(n); });
            }
            if (&type == &TypeOf<i32>())
            {
                return number([](f64 n) { return Variant::From<i32>(static_cast<i32>(n)); });
            }
            if (&type == &TypeOf<u32>())
            {
                return number([](f64 n) { return Variant::From<u32>(static_cast<u32>(n)); });
            }
            if (&type == &TypeOf<i64>())
            {
                return number([](f64 n) { return Variant::From<i64>(static_cast<i64>(n)); });
            }
            if (&type == &TypeOf<u64>())
            {
                return number([](f64 n) { return Variant::From<u64>(static_cast<u64>(n)); });
            }
            if (&type == &TypeOf<i16>())
            {
                return number([](f64 n) { return Variant::From<i16>(static_cast<i16>(n)); });
            }
            if (&type == &TypeOf<u16>())
            {
                return number([](f64 n) { return Variant::From<u16>(static_cast<u16>(n)); });
            }
            if (&type == &TypeOf<i8>())
            {
                return number([](f64 n) { return Variant::From<i8>(static_cast<i8>(n)); });
            }
            if (&type == &TypeOf<u8>())
            {
                return number([](f64 n) { return Variant::From<u8>(static_cast<u8>(n)); });
            }
            if (&type == &TypeOf<String>())
            {
                return value.IsString() ? Variant::From<String>(value.AsString()) : Variant{};
            }
            if (&type == &TypeOf<Guid>())
            {
                Guid id;
                return (value.IsString() && Guid::TryParse(value.AsString().AsView(), id))
                           ? Variant::From<Guid>(id)
                           : Variant{};
            }
            const auto component = [&](usize i) -> f32
            { return static_cast<f32>(value.At(static_cast<i32>(i)).AsNumber()); };
            const auto numbers = [&](usize count)
            {
                if (!value.IsArray() || static_cast<usize>(value.Count()) != count)
                {
                    return false;
                }
                for (usize i = 0; i < count; ++i)
                {
                    if (!value.At(static_cast<i32>(i)).IsNumber())
                    {
                        return false;
                    }
                }
                return true;
            };
            if (&type == &TypeOf<Float2>())
            {
                return numbers(2) ? Variant::From<Float2>(Float2{component(0), component(1)}) : Variant{};
            }
            if (&type == &TypeOf<Float3>())
            {
                return numbers(3) ? Variant::From<Float3>(Float3{component(0), component(1), component(2)})
                                  : Variant{};
            }
            if (&type == &TypeOf<Float4>())
            {
                return numbers(4) ? Variant::From<Float4>(
                                        Float4{component(0), component(1), component(2), component(3)})
                                  : Variant{};
            }
            if (&type == &TypeOf<Quaternion>())
            {
                return numbers(4) ? Variant::From<Quaternion>(
                                        Quaternion{component(0), component(1), component(2), component(3)})
                                  : Variant{};
            }
            if (&type == &TypeOf<Color>())
            {
                return numbers(4) ? Variant::From<Color>(
                                        Color{component(0), component(1), component(2), component(3)})
                                  : Variant{};
            }
            return Variant{};
        }

        /// The JSON shape a leaf property takes, for a refusal that teaches.
        StringView LeafShape(const TypeInfo& type)
        {
            if (&type == &TypeOf<bool>())
            {
                return u8"a boolean";
            }
            if (&type == &TypeOf<String>())
            {
                return u8"a string";
            }
            if (&type == &TypeOf<Guid>())
            {
                return u8"a guid string";
            }
            if (&type == &TypeOf<Float2>())
            {
                return u8"[x, y]";
            }
            if (&type == &TypeOf<Float3>())
            {
                return u8"[x, y, z]";
            }
            if (&type == &TypeOf<Float4>() || &type == &TypeOf<Quaternion>())
            {
                return u8"[x, y, z, w]";
            }
            if (&type == &TypeOf<Color>())
            {
                return u8"[r, g, b, a]";
            }
            if (&type == &TypeOf<f32>() || &type == &TypeOf<f64>() || &type == &TypeOf<i32>() ||
                &type == &TypeOf<u32>() || &type == &TypeOf<i64>() || &type == &TypeOf<u64>() ||
                &type == &TypeOf<i16>() || &type == &TypeOf<u16>() || &type == &TypeOf<i8>() ||
                &type == &TypeOf<u8>())
            {
                return u8"a number";
            }
            return StringView();
        }

        /// An enumerator by name or by number; false when neither names one.
        bool EnumValueOf(const TypeInfo& type, const JsonValue& value, i64& out)
        {
            for (u32 i = 0; i < type.enumeratorCount; ++i)
            {
                const EnumValue& enumerator = type.enumerators[i];
                if ((value.IsString() &&
                     value.AsString().AsView() ==
                         StringView(reinterpret_cast<const utf8char*>(enumerator.name))) ||
                    (value.IsNumber() && static_cast<i64>(value.AsNumber()) == enumerator.value))
                {
                    out = enumerator.value;
                    return true;
                }
            }
            return false;
        }

        String EnumeratorNames(const TypeInfo& type)
        {
            String names;
            for (u32 i = 0; i < type.enumeratorCount; ++i)
            {
                if (i > 0)
                {
                    names += u8", ";
                }
                names += StringView(reinterpret_cast<const utf8char*>(type.enumerators[i].name));
            }
            return names;
        }

        constexpr StringView kPageArgument =
            u8"the scene or prefab page's asset guid (default: the active page)";

        // === the viewport: its camera, and a capture of what it shows ===

        JsonValue CameraJson(const AddressedPage& addressed, const EditorCamera& camera)
        {
            JsonValue out = JsonValue::MakeObject();
            out.Set(u8"page", PageJson(*addressed.page));
            out.Set(u8"position", Float3Json(camera.position));
            out.Set(u8"yawDegrees", JsonValue::MakeNumber(static_cast<f64>(RadiansToDegrees(camera.yaw))));
            out.Set(u8"pitchDegrees",
                    JsonValue::MakeNumber(static_cast<f64>(RadiansToDegrees(camera.pitch))));
            out.Set(u8"forward", Float3Json(camera.Forward()));
            out.Set(u8"focusDistance", JsonValue::MakeNumber(static_cast<f64>(camera.focusDistance)));
            return out;
        }

        bool ReadFloat3(const JsonValue& value, Float3& out)
        {
            if (!value.IsArray() || value.Count() != 3)
            {
                return false;
            }
            for (i64 i = 0; i < 3; ++i)
            {
                if (!value.At(i).IsNumber())
                {
                    return false;
                }
            }
            out = Float3{static_cast<f32>(value.At(0).AsNumber()), static_cast<f32>(value.At(1).AsNumber()),
                         static_cast<f32>(value.At(2).AsNumber())};
            return true;
        }

        /// A page title as a file stem: letters, digits, '-' and '_' kept, the rest '_'.
        String FileStemOf(StringView title)
        {
            String stem;
            for (usize i = 0; i < title.Size(); ++i)
            {
                const utf8char c = title[i];
                const bool keep = (c >= u8'a' && c <= u8'z') || (c >= u8'A' && c <= u8'Z') ||
                                  (c >= u8'0' && c <= u8'9') || c == u8'-' || c == u8'_';
                stem += keep ? c : u8'_';
            }
            return stem.IsEmpty() ? String(u8"page") : stem;
        }

        /// One viewport_screenshot in flight: the tool is re-entered every pump with the same
        /// arguments until the page reports the capture written (or it gives up).
        struct PendingCapture
        {
            EditorPage* page = nullptr;
            String path;
            u32 pumps = 0;
            u32 serial = 0; // per host, so two captures of one page never share a default name
        };
        constexpr u32 kCapturePumpLimit = 600; // frames: ten seconds at 60 Hz, then the tool gives up
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

        server.RegisterTool(
            u8"component_set",
            u8"Set ONE reflected property of an entity's component on a scene page, through the "
            u8"editor's undo path: one undo step per call, labelled mcp, the page marked dirty, "
            u8"nothing saved (file.save / the page's Save does that). `value` takes the shape "
            u8"entity_inspect shows: numbers, booleans, strings, [x,y,z] vectors, [r,g,b,a] "
            u8"colors, [x,y,z,w] quaternions, an enumerator's name, an asset guid for a "
            u8"reference, an entity guid (or null) for an entity reference. REFUSED while the "
            u8"page simulates, on a read-only property, on a nested structure or a list (not "
            u8"writable here yet), and on a value of the wrong shape - nothing changes then. "
            u8"Returns the property as entity_inspect reads it after the write.",
            SchemaBuilder()
                .Str(u8"page", kPageArgument)
                .Str(u8"entity", u8"the entity's guid (default: the page's primary selection)")
                .Str(u8"component", u8"the component, as entity_inspect names it: its `type` "
                                    u8"(\"light\", \"physics.RigidBody\") or `typeName`",
                     true)
                .Str(u8"property", u8"the property's name, as entity_inspect shows it", true)
                .Build(),
            ToolAnnotations::Adjusts(),
            [ctx](const JsonValue& args) -> ToolResult
            {
                Result<AddressedPage, String> addressed = ResolveScenePage(*ctx, args);
                if (!addressed.HasValue())
                {
                    return Err(Move(addressed.Error()));
                }
                ISceneEditorPage& page = *addressed.Value().scene;
                const StringView title = addressed.Value().page->Title();
                if (page.IsSimulating())
                {
                    return Err(Format(u8"page '{}' is simulating - edits are locked until "
                                      u8"simulate_stop",
                                      title));
                }
                SceneEditContext& edit = page.EditContext();
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
                                          title));
                    }
                    id = *primary;
                }
                const scene::EntityHandle handle = edit.Resolve(id);
                if (!edit.Scene().IsValid(handle))
                {
                    utf8char text[37];
                    id.ToChars(text);
                    return Err(Format(u8"no entity with guid '{}' in page '{}'", StringView(text, 36),
                                      title));
                }
                const String component = args.Get(u8"component").AsString();
                scene::ComponentManagerBase* manager =
                    FindComponentManager(edit.Scene(), handle, component.AsView());
                if (manager == nullptr || manager->ComponentType() == nullptr)
                {
                    return Err(Format(u8"entity '{}' has no reflected component '{}' "
                                      u8"(entity_inspect lists its components)",
                                      edit.Scene().GetEntityName(handle), component.AsView()));
                }
                const TypeInfo& type = *manager->ComponentType();
                const String property = args.Get(u8"property").AsString();
                const PropertyInfo* prop = FindProperty(
                    type, reinterpret_cast<const char*>(property.CStr()));
                if (prop == nullptr || prop->type == nullptr)
                {
                    return Err(Format(u8"component '{}' has no property '{}' (entity_inspect "
                                      u8"lists them)",
                                      component.AsView(), property.AsView()));
                }
                if ((static_cast<u32>(prop->flags) & static_cast<u32>(PropertyFlags::ReadOnly)) != 0)
                {
                    return Err(Format(u8"property '{}' of '{}' is read-only", property.AsView(),
                                      component.AsView()));
                }
                if (IsNested(*prop))
                {
                    return Err(Format(u8"property '{}' of '{}' is a {} - not writable through "
                                      u8"component_set yet (scene_write edits the source)",
                                      property.AsView(), component.AsView(),
                                      prop->type->container != nullptr ? StringView(u8"list")
                                                                       : StringView(u8"structure")));
                }
                const JsonValue value = args.Get(u8"value");
                const TypeInfo& propType = *prop->type;

                // Shape the value first, so a refusal touches nothing; then ONE command in a
                // locked group labelled mcp - one undo step per call that neither the user's
                // scrub of the same property nor the next call merges into.
                Function<void()> write;
                if (IsReferenceType(propType))
                {
                    Guid target;
                    if (!value.IsNull() &&
                        (!value.IsString() || !Guid::TryParse(value.AsString().AsView(), target)))
                    {
                        return Err(Format(u8"property '{}' is a reference - `value` is an asset "
                                          u8"guid or null",
                                          property.AsView()));
                    }
                    SceneEditContext* editPtr = &edit;
                    EditorContext* context = ctx;
                    const TypeInfo* typePtr = &type;
                    const char* name = prop->name;
                    write = [editPtr, context, id, typePtr, name, target]()
                    {
                        editPtr->SetComponentReference(id, typePtr, ComponentPropertyPath{}, name,
                                                       target, context->Resources());
                    };
                }
                else if (propType.enumeratorCount > 0)
                {
                    i64 enumerator = 0;
                    if (!EnumValueOf(propType, value, enumerator))
                    {
                        return Err(Format(u8"property '{}' takes one of: {}", property.AsView(),
                                          EnumeratorNames(propType).AsView()));
                    }
                    SceneEditContext* editPtr = &edit;
                    const TypeInfo* typePtr = &type;
                    const char* name = prop->name;
                    write = [editPtr, id, typePtr, name, enumerator]()
                    { editPtr->SetComponentPropertyRaw(id, typePtr, name, enumerator); };
                }
                else if (&propType == &TypeOf<scene::EntityRef>())
                {
                    Guid target;
                    if (!value.IsNull() &&
                        (!value.IsString() || !Guid::TryParse(value.AsString().AsView(), target)))
                    {
                        return Err(Format(u8"property '{}' is an entity reference - `value` is an "
                                          u8"entity guid or null",
                                          property.AsView()));
                    }
                    SceneEditContext* editPtr = &edit;
                    const TypeInfo* typePtr = &type;
                    const char* name = prop->name;
                    write = [editPtr, id, typePtr, name, target]()
                    { editPtr->SetComponentEntityRef(id, typePtr, name, target); };
                }
                else
                {
                    Variant leaf = LeafVariant(propType, value);
                    if (leaf.IsEmpty())
                    {
                        const StringView shape = LeafShape(propType);
                        if (shape.IsEmpty())
                        {
                            return Err(Format(u8"property '{}' of '{}' is a {} - not writable "
                                              u8"through component_set yet",
                                              property.AsView(), component.AsView(),
                                              StringView(reinterpret_cast<const utf8char*>(propType.name))));
                        }
                        return Err(Format(u8"property '{}' of '{}' takes {} - `value` has the "
                                          u8"wrong shape (entity_inspect shows the current value)",
                                          property.AsView(), component.AsView(), shape));
                    }
                    SceneEditContext* editPtr = &edit;
                    const TypeInfo* typePtr = &type;
                    const char* name = prop->name;
                    write = [editPtr, id, typePtr, name, leaf = Move(leaf)]()
                    { editPtr->SetComponentProperty(id, typePtr, name, leaf); };
                }
                EditorCommandStack& commands = edit.Commands();
                const i64 before = commands.UndoIndex();
                commands.BeginGroup(u8"mcp");
                write();
                commands.EndGroup();
                commands.LockGroup();
                if (commands.UndoIndex() == before)
                {
                    return Err(Format(u8"property '{}' of '{}' could not be set (the command was "
                                      u8"refused; see log_read)",
                                      property.AsView(), component.AsView()));
                }
                const Instance instance = manager->GetComponentInstance(handle);
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"page", PageJson(*addressed.Value().page));
                out.Set(u8"entity", GuidJson(id));
                out.Set(u8"component", JsonValue::MakeString(String(manager->SerializationTypeId())));
                out.Set(u8"property", JsonValue::MakeString(property));
                out.Set(u8"value", PropertyJson(*prop, instance));
                out.Set(u8"undoSteps", JsonValue::MakeNumber(1));
                return out;
            });
        server.RegisterTool(
            u8"viewport_camera_get",
            u8"The pose a scene page's viewport looks from: the editor camera's position, yaw and "
            u8"pitch in degrees (yaw 0 looks down -Z, positive pitch looks up), its forward vector "
            u8"and its orbit focus distance. Defaults to the active page.",
            SchemaBuilder().Str(u8"page", kPageArgument).Build(), ToolAnnotations::ReadOnly(),
            [ctx](const JsonValue& args) -> ToolResult
            {
                Result<AddressedPage, String> addressed = ResolveScenePage(*ctx, args);
                if (!addressed.HasValue())
                {
                    return Err(Move(addressed.Error()));
                }
                const EditorCamera* camera = addressed.Value().scene->ViewportCamera();
                if (camera == nullptr)
                {
                    return Err(Format(u8"page '{}' has no viewport", addressed.Value().page->Title()));
                }
                return CameraJson(addressed.Value(), *camera);
            });

        server.RegisterTool(
            u8"viewport_camera_set",
            u8"Move a scene page's viewport camera - to look at something from somewhere specific "
            u8"before a viewport_screenshot, or to show the user a spot. Sets what is given: "
            u8"`position` ([x, y, z]), then `yawDegrees` / `pitchDegrees`, then `lookAt` "
            u8"([x, y, z]: aims from the position at that point, horizon level, and moves the "
            u8"orbit focus there - it wins over yaw and pitch). Nothing given changes nothing. "
            u8"Returns the pose as viewport_camera_get does. Editor state only: no scene edit, "
            u8"no undo step.",
            SchemaBuilder()
                .Str(u8"page", kPageArgument)
                .Arr(u8"position", u8"number", u8"the camera position [x, y, z]")
                .Number(u8"yawDegrees", u8"rotation about the up axis; 0 looks down -Z")
                .Number(u8"pitchDegrees", u8"tilt; positive looks up, clamped short of straight up or down")
                .Arr(u8"lookAt", u8"number", u8"the point [x, y, z] to aim at from the position")
                .Build(),
            ToolAnnotations::Adjusts(),
            [ctx](const JsonValue& args) -> ToolResult
            {
                Result<AddressedPage, String> addressed = ResolveScenePage(*ctx, args);
                if (!addressed.HasValue())
                {
                    return Err(Move(addressed.Error()));
                }
                EditorCamera* camera = addressed.Value().scene->ViewportCamera();
                if (camera == nullptr)
                {
                    return Err(Format(u8"page '{}' has no viewport", addressed.Value().page->Title()));
                }
                // Shape everything first: a refusal changes nothing.
                Float3 position;
                Float3 lookAt;
                const bool hasPosition = args.Has(u8"position");
                const bool hasLookAt = args.Has(u8"lookAt");
                if (hasPosition && !ReadFloat3(args.Get(u8"position"), position))
                {
                    return Err(String(u8"`position` takes [x, y, z]"));
                }
                if (hasLookAt && !ReadFloat3(args.Get(u8"lookAt"), lookAt))
                {
                    return Err(String(u8"`lookAt` takes [x, y, z]"));
                }
                if ((args.Has(u8"yawDegrees") && !args.Get(u8"yawDegrees").IsNumber()) ||
                    (args.Has(u8"pitchDegrees") && !args.Get(u8"pitchDegrees").IsNumber()))
                {
                    return Err(String(u8"`yawDegrees` and `pitchDegrees` take a number"));
                }
                if (hasPosition)
                {
                    camera->position = position;
                }
                if (args.Has(u8"yawDegrees"))
                {
                    camera->yaw = DegreesToRadians(static_cast<f32>(args.Get(u8"yawDegrees").AsNumber()));
                }
                if (args.Has(u8"pitchDegrees"))
                {
                    // Short of the poles, as the mouse look is, so the up vector stays defined.
                    const f32 limit = DegreesToRadians(89.0f);
                    camera->pitch = Clamp(
                        DegreesToRadians(static_cast<f32>(args.Get(u8"pitchDegrees").AsNumber())), -limit,
                        limit);
                }
                if (hasLookAt)
                {
                    camera->LookAt(lookAt);
                }
                return CameraJson(addressed.Value(), *camera);
            });

        auto pending = MakeUnique<PendingCapture>(context.Allocator());
        PendingCapture* pendingPtr = pending.Get();
        server.RegisterTool(
            u8"viewport_screenshot",
            u8"What a scene page's viewport shows, as a PNG file: the view as the user sees it at the "
            u8"viewport's size - the scene from the editor camera (viewport_camera_set moves it), "
            u8"with the grid, the gizmo and markers of the selection and the tool's overlay text. "
            u8"Brings the page to front (a hidden viewport never renders), waits for the next frame "
            u8"and the GPU, then returns {page, path, width, height}; read the file. `path` is where "
            u8"to write (an existing directory; default: <user-data>/screenshots/<page>-<pid>-<n>.png). "
            u8"Gives up after ten seconds without a rendered frame.",
            SchemaBuilder()
                .Str(u8"page", kPageArgument)
                .Str(u8"path", u8"the PNG to write (default: a new file under <user-data>/screenshots)")
                .Build(),
            ToolAnnotations::Creates(),
            [ctx, pendingPtr, keep = Move(pending)](const JsonValue& args) -> foundation::mcp::ToolOutcome
            {
                Result<AddressedPage, String> addressed = ResolveScenePage(*ctx, args);
                if (!addressed.HasValue())
                {
                    return Err(Move(addressed.Error()));
                }
                ISceneEditorPage* scene = addressed.Value().scene;
                EditorPage* page = addressed.Value().page;
                if (pendingPtr->page == page)
                {
                    // Re-entered: the same call, one pump later.
                    const ViewportCapture& capture = scene->LastViewportCapture();
                    ++pendingPtr->pumps;
                    if (capture.state == ViewportCaptureState::Written)
                    {
                        JsonValue out = JsonValue::MakeObject();
                        out.Set(u8"page", PageJson(*page));
                        out.Set(u8"path", JsonValue::MakeString(capture.path));
                        out.Set(u8"width", JsonValue::MakeNumber(static_cast<f64>(capture.width)));
                        out.Set(u8"height", JsonValue::MakeNumber(static_cast<f64>(capture.height)));
                        pendingPtr->page = nullptr;
                        return out;
                    }
                    if (capture.state == ViewportCaptureState::Failed)
                    {
                        pendingPtr->page = nullptr;
                        return Err(Format(u8"the capture of page '{}' failed (log_read, category "
                                          u8"Screenshot, says why)",
                                          page->Title()));
                    }
                    if (pendingPtr->pumps > kCapturePumpLimit)
                    {
                        pendingPtr->page = nullptr;
                        return Err(Format(u8"page '{}' rendered no frame in ten seconds - is its "
                                          u8"viewport visible (an editor window minimised or hidden)?",
                                          page->Title()));
                    }
                    return foundation::mcp::ToolOutcome::NotFinished();
                }
                if (scene->ViewportCamera() == nullptr)
                {
                    return Err(Format(u8"page '{}' has no viewport", page->Title()));
                }
                String path = args.Get(u8"path").AsString();
                if (path.IsEmpty())
                {
                    const String dir = PathJoin(GetUserDataDirectory().AsView(), u8"screenshots");
                    if (!CreateDirectories(dir.AsView()))
                    {
                        return Err(Format(u8"could not create '{}'", dir.AsView()));
                    }
                    ++pendingPtr->serial;
                    path = PathJoin(dir.AsView(), Format(u8"{}-{}-{}.png", FileStemOf(page->Title()).AsView(),
                                                         ProcessId(), pendingPtr->serial)
                                                      .AsView());
                }
                ctx->RevealPage(page); // to front: a background tab's viewport never renders
                const Status requested = scene->RequestViewportCapture(path.AsView());
                if (!requested.IsOk())
                {
                    return Err(Format(u8"page '{}' has no viewport", page->Title()));
                }
                pendingPtr->page = page;
                pendingPtr->path = Move(path);
                pendingPtr->pumps = 0;
                return foundation::mcp::ToolOutcome::NotFinished();
            });

    }
}
