// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Integration.Mcp - the scene format reference (Documentation/Specs/scene-format-reference.md,
// P1): the generated schema's joins, the example's round trip through the real reader, the
// determinism contract, and the live surface every host serves.

#include <doctest/doctest.h>
#include <filesystem>
#include "Core/Prelude.h"
import foundation.core;
import foundation.json;
import foundation.content;
import foundation.vfs;
import foundation.scene;
import foundation.scene.resource;
import foundation.resource;
import foundation.geometry.resource;
import foundation.script.resource;
import foundation.mcp;
import pipeline.core;
import pipeline.registration;
import engine.composition;
import editor.project;
import editor.mcp;

using namespace foundation::core;
using namespace foundation::mcp;
namespace json = foundation::json;
using foundation::json::JsonValue;
namespace scene = foundation::scene;
namespace content = foundation::content;

namespace
{
    constexpr const char* kDbDir = "mcp_scene_reference_db";

    void RemoveDb()
    {
        std::error_code ec;
        std::filesystem::remove_all(kDbDir, ec);
    }

    // A content database + a ResourceManager carrying ONE factory (StaticMesh), so the reference
    // join has a resolvable target and every other Ref<T> an unresolvable one.
    struct Fixture
    {
        foundation::vfs::NativeFileSystem mount{u8"mcp_scene_reference_db", DefaultAllocator()};
        content::ContentDatabase db{DefaultAllocator(), mount, BinarySerializerFactory(), u8".rasset"};
        foundation::resource::ResourceManager resources{DefaultAllocator(), db};
        foundation::geometry::StaticMeshFactory meshFactory{DefaultAllocator()};
        pipeline::BuilderRegistry builders{DefaultAllocator()};

        Fixture()
        {
            RemoveDb();
            pipeline::RegisterAllBuilders(builders);
            resources.AddFactory(&meshFactory);
        }
        ~Fixture() { RemoveDb(); }

        [[nodiscard]] editor::mcp::SceneReference Generate(bool withFactories = true) const
        {
            return editor::mcp::GenerateSceneReference(DefaultAllocator(), builders,
                                                       withFactories ? &resources : nullptr);
        }
    };

    JsonValue Field(const JsonValue& fields, StringView key)
    {
        for (const JsonValue& field : fields.Items())
        {
            if (field.Get(u8"key").AsString().AsView() == key)
            {
                return field;
            }
        }
        return JsonValue::MakeNull();
    }

    bool Lists(const JsonValue& strings, StringView wanted)
    {
        for (const JsonValue& item : strings.Items())
        {
            if (item.AsString().AsView() == wanted)
            {
                return true;
            }
        }
        return false;
    }

    JsonValue CallResponse(McpServer& s, StringView tool, JsonValue arguments)
    {
        JsonValue params = JsonValue::MakeObject();
        params.Set(u8"name", JsonValue::MakeString(String(tool)));
        params.Set(u8"arguments", Move(arguments));
        JsonValue req = JsonValue::MakeObject();
        req.Set(u8"jsonrpc", JsonValue::MakeString(u8"2.0"));
        req.Set(u8"id", JsonValue::MakeNumber(1));
        req.Set(u8"method", JsonValue::MakeString(u8"tools/call"));
        req.Set(u8"params", Move(params));
        LineOutcome line = s.HandleLine(req.ToString().AsView());
        REQUIRE(line.state == LineState::Answered);
        return json::Parse(line.response.AsView()).value;
    }

    JsonValue CallOk(McpServer& s, StringView tool, JsonValue arguments)
    {
        JsonValue resp = CallResponse(s, tool, Move(arguments));
        REQUIRE(resp.Has(u8"result"));
        CHECK(resp.Get(u8"result").Get(u8"isError").AsBool() == false);
        return JsonValue::Parse(resp.Get(u8"result").Get(u8"content").At(0).Get(u8"text").AsString());
    }

    String Response(McpServer& s, StringView line)
    {
        LineOutcome out = s.HandleLine(line);
        REQUIRE(out.state == LineState::Answered);
        return Move(out.response);
    }

    String ReadResource(McpServer& s, StringView uri)
    {
        const String line = Response(
            s, Format(u8"{}{}{}",
                      StringView(u8"{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"resources/read\","
                                 u8"\"params\":{\"uri\":\""),
                      uri, StringView(u8"\"}}"))
                   .AsView());
        return json::Parse(line.AsView()).value.Get(u8"result").Get(u8"contents").At(0).Get(u8"text").AsString();
    }
}

TEST_CASE("integration.mcp: scene reference - the physics settings block lists groupNames in wire "
          "order and reports it unreflected")
{
    Fixture f;
    const editor::mcp::SceneReference reference = f.Generate();
    const JsonValue physics = editor::mcp::FindSchemaEntry(reference.schema, u8"physics");
    REQUIRE(physics.IsObject());
    CHECK(physics.Get(u8"system").AsString() == StringView(u8"physics"));
    CHECK(physics.Get(u8"dataVersions").Count() >= 1);
    const JsonValue groupNames = Field(physics.Get(u8"fields"), u8"groupNames");
    REQUIRE(groupNames.IsObject());
    CHECK(groupNames.Get(u8"kind").AsString() == StringView(u8"array"));
    CHECK(groupNames.Get(u8"count").AsNumber() == doctest::Approx(0.0)); // empty at default
    CHECK(Lists(physics.Get(u8"unreflected"), u8"groupNames"));
    // Nested math values are not "unreflected": Color/Float3 have no reflected fields to miss.
    CHECK_FALSE(Lists(physics.Get(u8"unreflected"), u8"x"));
    // The lookup folds case and also answers to the settings type's name.
    CHECK(editor::mcp::FindSchemaEntry(reference.schema, u8"PHYSICS").IsObject());
    CHECK(editor::mcp::FindSchemaEntry(reference.schema, physics.Get(u8"type").Get(u8"name").AsString().AsView()).IsObject());
    CHECK(editor::mcp::FindSchemaEntry(reference.schema, u8"no-such-block").IsNull());
}

TEST_CASE("integration.mcp: scene reference - an EntityRef field is `ref: entity`; a Ref<StaticMesh> "
          "field is joined to its asset type through the factory and the builder, or left as the "
          "resource name alone when the host has no factory for it")
{
    Fixture f;
    const editor::mcp::SceneReference reference = f.Generate();

    // mesh.mesh: Ref<StaticMesh> -> StaticMeshFactory -> StaticMeshSource -> the builder -> its asset.
    const JsonValue mesh = editor::mcp::FindSchemaEntry(reference.schema, u8"mesh");
    REQUIRE(mesh.IsObject());
    const JsonValue meshField = Field(mesh.Get(u8"fields"), u8"mesh");
    REQUIRE(meshField.IsObject());
    CHECK(meshField.Get(u8"kind").AsString() == StringView(u8"guid"));
    CHECK(meshField.Get(u8"ref").Get(u8"resource").AsString() == StringView(u8"StaticMesh"));
    CHECK(meshField.Get(u8"ref").Get(u8"asset").AsString() == StringView(u8"StaticMeshAsset"));
    CHECK_FALSE(Lists(mesh.Get(u8"unreflected"), u8"mesh"));

    // Entity references are `ref: entity`: a single one is a guid field, a list of them an array
    // annotated through its element type (the animator's mesh targets).
    bool sawEntityRef = false;
    bool sawEntityRefList = false;
    for (const JsonValue& component : reference.schema.Get(u8"components").Items())
    {
        for (const JsonValue& field : component.Get(u8"fields").Items())
        {
            if (field.Get(u8"ref").IsString() && field.Get(u8"ref").AsString() == StringView(u8"entity"))
            {
                const String kind = field.Get(u8"kind").AsString();
                CHECK((kind == StringView(u8"guid") || kind == StringView(u8"array")));
                sawEntityRef = sawEntityRef || kind == StringView(u8"guid");
                sawEntityRefList = sawEntityRefList || kind == StringView(u8"array");
            }
        }
    }
    CHECK(sawEntityRef);
    CHECK(sawEntityRefList);

    // Enum names ride along: the light's type field names its values.
    const JsonValue light = editor::mcp::FindSchemaEntry(reference.schema, u8"light");
    REQUIRE(light.IsObject());
    const JsonValue lightType = Field(light.Get(u8"fields"), u8"type");
    REQUIRE(lightType.IsObject());
    CHECK(lightType.Get(u8"enum").Count() >= 2);
    CHECK(Field(light.Get(u8"fields"), u8"range").Get(u8"range").Get(u8"max").AsNumber() == doctest::Approx(500.0));
    // The light's colour is a math value: an object of r, g, b, a, none of them "unreflected".
    CHECK(Field(light.Get(u8"fields"), u8"color").Get(u8"fields").Count() == 4);
    CHECK_FALSE(Lists(light.Get(u8"unreflected"), u8"r"));

    // No factories (the stdio host today): the resource name stands alone, counted as unreflected.
    const editor::mcp::SceneReference bare = f.Generate(/*withFactories=*/false);
    const JsonValue bareMesh = Field(editor::mcp::FindSchemaEntry(bare.schema, u8"mesh").Get(u8"fields"), u8"mesh");
    CHECK(bareMesh.Get(u8"ref").Get(u8"resource").AsString() == StringView(u8"StaticMesh"));
    CHECK_FALSE(bareMesh.Get(u8"ref").Has(u8"asset"));
    CHECK(Lists(editor::mcp::FindSchemaEntry(bare.schema, u8"mesh").Get(u8"unreflected"), u8"mesh"));
}

TEST_CASE("integration.mcp: scene reference - the script override section's worked hash is "
          "ScriptPropertyNameHash of its name, and every authored kind names its payload key")
{
    Fixture f;
    const editor::mcp::SceneReference reference = f.Generate();
    const JsonValue overrides = reference.schema.Get(u8"scriptOverrides");
    REQUIRE(overrides.IsObject());
    const JsonValue example = overrides.Get(u8"hash").Get(u8"example");
    const String name = example.Get(u8"name").AsString();
    CHECK_FALSE(name.IsEmpty());
    CHECK(example.Get(u8"nameHash").AsString() ==
          Format(u8"{}", foundation::script::ScriptPropertyNameHash(name.AsView())).AsView());

    const JsonValue kinds = overrides.Get(u8"kinds");
    CHECK(static_cast<usize>(kinds.Count()) == foundation::script::ScriptPropertyTypeNames().Size());
    const auto payloadKey = [&kinds](StringView kindName) -> String
    {
        for (const JsonValue& kind : kinds.Items())
        {
            if (kind.Get(u8"name").AsString().AsView() == kindName)
            {
                return kind.Get(u8"payload").At(0).Get(u8"key").AsString();
            }
        }
        return String();
    };
    CHECK(payloadKey(u8"float") == StringView(u8"number"));
    CHECK(payloadKey(u8"bool") == StringView(u8"boolean"));
    CHECK(payloadKey(u8"string") == StringView(u8"text"));
    CHECK(payloadKey(u8"vec3") == StringView(u8"vector"));
    CHECK(payloadKey(u8"entity") == StringView(u8"guid"));
    CHECK(payloadKey(u8"asset") == StringView(u8"guid"));
    // The record framing as the wire has it: the hash, then the value's tag inline (a struct
    // field writes its fields under the parent), then the payload key the tag selects.
    const JsonValue record = overrides.Get(u8"record");
    REQUIRE(record.Count() == 2);
    CHECK(record.At(0).Get(u8"key").AsString() == StringView(u8"nameHash"));
    CHECK(record.At(0).Get(u8"kind").AsString() == StringView(u8"u64"));
    CHECK(record.At(1).Get(u8"key").AsString() == StringView(u8"kind"));
    CHECK(record.At(1).Get(u8"kind").AsString() == StringView(u8"u8"));
    // The example's behaviour carries one override per kind, hashed from "<kind>Value".
    CHECK(reference.exampleXml.AsView().ContainsIgnoreCase(
        Format(u8"{}", foundation::script::ScriptPropertyNameHash(u8"floatValue")).AsView()));
}

TEST_CASE("integration.mcp: scene reference - the example loads through LoadScene into the full "
          "composition and scene_validate finds it valid with no warnings")
{
    Fixture f;
    const editor::mcp::SceneReference reference = f.Generate();
    REQUIRE_FALSE(reference.exampleXml.IsEmpty());

    // Through the real reader: the example stored as a scene document loads with every record
    // routed to a manager.
    GlobalTypeRegistry().Register(scene::SceneDocument::StaticType());
    RegisterSerializable<scene::SceneDocument>();
    content::Instance* instance =
        f.db.RootGroup()->CreateInstance(u8"Reference", scene::SceneDocument::StaticType());
    REQUIRE(instance != nullptr);
    scene::SceneDocument doc;
    doc.name = String(u8"SceneReference");
    REQUIRE(instance->WriteObject(doc).IsOk());
    REQUIRE(instance
                ->WriteData(u8"scene",
                            Span<const byte>{reinterpret_cast<const byte*>(reference.exampleXml.CStr()),
                                             reference.exampleXml.Size()},
                            content::StreamEncoding::Text)
                .IsOk());
    scene::Scene loaded(DefaultAllocator());
    engine::AddAllSceneManagers(loaded);
    REQUIRE(scene::LoadScene(*instance, loaded).IsOk());
    // Root, Child, and one entity per serializable manager (the schema's components list).
    CHECK(loaded.EntityCount() == 2 + static_cast<u32>(reference.schema.Get(u8"components").Count()));
    for (const JsonValue& component : reference.schema.Get(u8"components").Items())
    {
        CHECK(component.Get(u8"recorded").AsBool(true)); // every manager took its default component
    }

    // Through the tool an agent loops on.
    McpServer server;
    editor::mcp::ProjectSession session;
    editor::mcp::RegisterSceneTools(server, session);
    JsonValue args = JsonValue::MakeObject();
    args.Set(u8"xml", JsonValue::MakeString(reference.exampleXml));
    const JsonValue report = CallOk(server, u8"scene_validate", Move(args));
    CHECK(report.Get(u8"valid").AsBool() == true);
    CHECK(report.Get(u8"warnings").Count() == 0);
    CHECK(report.Get(u8"entityCount").AsNumber() == doctest::Approx(static_cast<f64>(loaded.EntityCount())));
}

TEST_CASE("integration.mcp: scene reference - two generations are byte identical")
{
    Fixture f;
    const editor::mcp::SceneReference first = f.Generate();
    const editor::mcp::SceneReference second = f.Generate();
    CHECK(first.exampleXml == second.exampleXml);
    CHECK(first.schemaJson == second.schemaJson);
    CHECK_FALSE(first.schemaJson.IsEmpty());
    // The format section names the stream as the writer lays it out.
    const JsonValue format = first.schema.Get(u8"format");
    CHECK(format.Get(u8"version").AsNumber() == doctest::Approx(3.0));
    const JsonValue sections = format.Get(u8"sections");
    REQUIRE(sections.Count() >= 6);
    CHECK(sections.At(0).Get(u8"key").AsString() == StringView(u8"magic"));
    CHECK(sections.At(1).Get(u8"key").AsString() == StringView(u8"version"));
    CHECK(sections.At(3).Get(u8"key").AsString() == StringView(u8"entities"));
    CHECK(sections.At(4).Get(u8"key").AsString() == StringView(u8"components"));
    CHECK(sections.At(5).Get(u8"key").AsString() == StringView(u8"systemSettings"));
}

TEST_CASE("integration.mcp: scene reference - RegisterEngineTools serves both generated resources "
          "and component_schema, generated from the host's own registrations")
{
    pipeline::BuilderRegistry builders{DefaultAllocator()};
    pipeline::RegisterAllBuilders(builders);
    pipeline::ImporterRegistry importers{DefaultAllocator()};
    editor::EditorLogBuffer logBuffer{DefaultAllocator()};
    editor::mcp::ProjectSession session; // no resources: the stdio host's shape

    McpServer server;
    editor::mcp::InlineProjectOperations operations(session, builders, String(), String());
    editor::mcp::RegisterEngineTools(server, session, builders, importers, logBuffer,
                                     editor::mcp::EngineToolPaths{}, operations);

    const JsonValue resources =
        json::Parse(Response(server, u8"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"resources/list\"}").AsView())
            .value.Get(u8"result")
            .Get(u8"resources");
    String exampleMime;
    String schemaMime;
    for (const JsonValue& resource : resources.Items())
    {
        if (resource.Get(u8"uri").AsString().AsView() == editor::mcp::kSceneExampleUri)
        {
            exampleMime = resource.Get(u8"mimeType").AsString();
        }
        if (resource.Get(u8"uri").AsString().AsView() == editor::mcp::kSceneSchemaUri)
        {
            schemaMime = resource.Get(u8"mimeType").AsString();
        }
    }
    CHECK(exampleMime == StringView(u8"application/xml"));
    CHECK(schemaMime == StringView(u8"application/json"));

    // The served bytes are the generator's for this composition (deterministic, so a fresh
    // generation over the same registrations reproduces them).
    const editor::mcp::SceneReference expected =
        editor::mcp::GenerateSceneReference(DefaultAllocator(), builders, nullptr);
    CHECK(ReadResource(server, editor::mcp::kSceneExampleUri) == expected.exampleXml);
    CHECK(ReadResource(server, editor::mcp::kSceneSchemaUri) == expected.schemaJson);

    // component_schema("light") is the light's entry; an unknown name errs and lists what exists.
    JsonValue args = JsonValue::MakeObject();
    args.Set(u8"type", JsonValue::MakeString(u8"light"));
    const JsonValue light = CallOk(server, u8"component_schema", Move(args));
    CHECK(light.Get(u8"wireName").AsString() == StringView(u8"light"));
    CHECK(light.Get(u8"type").Get(u8"name").AsString() == StringView(u8"LightComponent"));
    CHECK(light.Get(u8"fields").Count() > 0);
    JsonValue bad = JsonValue::MakeObject();
    bad.Set(u8"type", JsonValue::MakeString(u8"no_such_component"));
    const JsonValue refused = CallResponse(server, u8"component_schema", Move(bad));
    CHECK(refused.Get(u8"result").Get(u8"isError").AsBool() == true);
    CHECK(refused.Get(u8"result").Get(u8"content").At(0).Get(u8"text").AsString().AsView().ContainsIgnoreCase(u8"light"));
}
