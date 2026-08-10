// Integration.Mcp - the golden agent-shaped tool-call flow.
//
// SUBJECT is the flow crossing collections: an agent drives the MCP protocol (foundation.mcp) to
// scaffold, open, and inspect a project on disk (editor.mcp -> the headless EditorProject over
// VFS + the content databases), all through JSON-RPC lines. This is the collection's first MCP
// resident; asset_import/asset_cook/scene_write extend it as those tools land.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "Core/Prelude.h"

import foundation.core;
import foundation.json;
import foundation.content;
import foundation.mcp;
import foundation.mcp.reflection;
import pipeline.core;
import pipeline.importer;
import pipeline.registration;
import engine.scriptsurface;
import foundation.mcp.script;
import foundation.script.wren;
import editor.mcp;

using namespace foundation::core;
using namespace foundation::mcp;
using foundation::json::JsonValue;
namespace json = foundation::json;

namespace
{
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
        Optional<String> line = s.HandleLine(req.ToString().AsView());
        REQUIRE(line.HasValue());
        return json::Parse(line.Value().AsView()).value;
    }

    JsonValue CallOk(McpServer& s, StringView tool, JsonValue arguments)
    {
        JsonValue resp = CallResponse(s, tool, Move(arguments));
        REQUIRE(resp.Has(u8"result"));
        CHECK(resp.Get(u8"result").Get(u8"isError").AsBool() == false);
        return JsonValue::Parse(resp.Get(u8"result").Get(u8"content").At(0).Get(u8"text").AsString());
    }

    JsonValue Obj() { return JsonValue::MakeObject(); }
    JsonValue With(JsonValue o, StringView k, StringView v)
    {
        o.Set(String(k), JsonValue::MakeString(String(v)));
        return o;
    }
}

TEST_CASE("integration.mcp: an agent creates, opens, and inspects a project via MCP tools")
{
    // Deterministic fixture: wipe any prior scratch project (.test-scratch is the CWD here).
    std::error_code ec;
    std::filesystem::remove_all("mcp_fixture_project", ec);

    McpServer server;
    editor::mcp::ProjectSession session;
    editor::mcp::RegisterProjectTools(server, session);
    RegisterReflectionTools(server); // the same host serves reflection + project tools

    // list advertises both tool families
    JsonValue tools = json::Parse(server.HandleLine(
                                        u8"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}")
                                       .Value()
                                       .AsView())
                          .value.Get(u8"result")
                          .Get(u8"tools");
    CHECK(tools.Count() >= 5); // project_create/open/info + type_list/type_info

    // create -> open -> info
    JsonValue created = CallOk(server, u8"project_create",
                               With(With(Obj(), u8"directory", u8"mcp_fixture_project"), u8"name",
                                    u8"Fixture"));
    CHECK(created.Get(u8"created").AsBool() == true);

    JsonValue opened =
        CallOk(server, u8"project_open", With(Obj(), u8"directory", u8"mcp_fixture_project"));
    CHECK(opened.Get(u8"name").AsString() == StringView(u8"Fixture"));

    JsonValue info = CallOk(server, u8"project_info", Obj());
    CHECK(info.Get(u8"name").AsString() == StringView(u8"Fixture"));
    CHECK(info.Get(u8"directory").AsString().Size() > 0u);
    CHECK(info.Get(u8"sourcesRoot").AsString().Size() > 0u);

    // The manifest really landed on disk.
    CHECK(std::filesystem::exists("mcp_fixture_project/Project.xml"));

    // Opening a nonexistent project is a TOOL error (real text), not a protocol fault.
    JsonValue bad =
        CallResponse(server, u8"project_open", With(Obj(), u8"directory", u8"nope_not_a_project"));
    CHECK(bad.Get(u8"result").Get(u8"isError").AsBool() == true);
}

TEST_CASE("integration.mcp: asset_list / asset_info read the open project's content DB")
{
    std::error_code ec;
    std::filesystem::remove_all("mcp_asset_project", ec);

    McpServer server;
    editor::mcp::ProjectSession session;
    editor::mcp::RegisterProjectTools(server, session);
    editor::mcp::RegisterAssetTools(server, session);

    CallOk(server, u8"project_create",
           With(With(Obj(), u8"directory", u8"mcp_asset_project"), u8"name", u8"Assets"));
    CallOk(server, u8"project_open", With(Obj(), u8"directory", u8"mcp_asset_project"));

    // A fresh project's source DB has no assets yet.
    CHECK(CallOk(server, u8"asset_list", Obj()).Get(u8"count").AsInt() == 0);

    // Seed one instance directly in the source DB, then read it back through the tools.
    Guid id;
    REQUIRE(Guid::TryParse(u8"12345678-1234-1234-1234-1234567890ab", id));
    session.project->SourceDb().RootGroup()->AddInstance(id, u8"hero", u8"rtti::test",
                                                         u8"TextureAsset");

    JsonValue list = CallOk(server, u8"asset_list", Obj());
    CHECK(list.Get(u8"count").AsInt() == 1);
    JsonValue first = list.Get(u8"assets").At(0);
    CHECK(first.Get(u8"name").AsString() == StringView(u8"hero"));
    CHECK(first.Get(u8"type").AsString() == StringView(u8"TextureAsset"));

    JsonValue seen = CallOk(server, u8"asset_info",
                            With(Obj(), u8"guid", u8"12345678-1234-1234-1234-1234567890ab"));
    CHECK(seen.Get(u8"name").AsString() == StringView(u8"hero"));
    CHECK(seen.Get(u8"typeNamespace").AsString() == StringView(u8"rtti::test"));

    // A well-formed but absent guid is a tool error (real text), not a protocol fault.
    JsonValue missing = CallResponse(server, u8"asset_info",
                                     With(Obj(), u8"guid", u8"00000000-0000-0000-0000-000000000000"));
    CHECK(missing.Get(u8"result").Get(u8"isError").AsBool() == true);

    // A malformed guid is likewise a tool error.
    JsonValue malformed =
        CallResponse(server, u8"asset_info", With(Obj(), u8"guid", u8"not-a-guid"));
    CHECK(malformed.Get(u8"result").Get(u8"isError").AsBool() == true);
}

TEST_CASE("integration.mcp: an agent imports a source file then cooks it via MCP tools")
{
    std::error_code ec;
    std::filesystem::remove_all("mcp_write_project", ec);
    std::filesystem::remove("mcp_write_source.wren", ec);

    // The host assembles the pipeline registries once (as Tools.Mcp does), from the composition
    // root - asset_cook/asset_import route through these.
    pipeline::RegisterPipelineTypes();
    pipeline::BuilderRegistry builders;
    pipeline::RegisterAllBuilders(builders);
    pipeline::ImporterRegistry importers;
    pipeline::RegisterAllImporters(importers);

    McpServer server;
    editor::mcp::ProjectSession session;
    editor::mcp::RegisterProjectTools(server, session);
    editor::mcp::RegisterAssetTools(server, session);
    editor::mcp::RegisterAssetWriteTools(server, session, builders, importers);

    CallOk(server, u8"project_create",
           With(With(Obj(), u8"directory", u8"mcp_write_project"), u8"name", u8"Write"));
    CallOk(server, u8"project_open", With(Obj(), u8"directory", u8"mcp_write_project"));

    // A real source file on disk (CWD is .test-scratch). A trivial Wren class cooks clean.
    {
        std::ofstream out("mcp_write_source.wren");
        out << "class Hello {\n  construct new() {}\n  greet() { System.print(\"hi\") }\n}\n";
    }
    const std::filesystem::path abs = std::filesystem::absolute("mcp_write_source.wren");
    const String absPath(reinterpret_cast<const utf8char*>(abs.string().c_str()));

    // Import routes by extension through the shared importer set into the source DB.
    JsonValue imported = CallOk(server, u8"asset_import",
                                With(With(Obj(), u8"source", absPath.AsView()), u8"group",
                                     u8"scripts"));
    CHECK(imported.Get(u8"type").AsString() == StringView(u8"ScriptClassAsset"));
    CHECK(imported.Get(u8"name").AsString() == StringView(u8"mcp_write_source"));
    CHECK(imported.Get(u8"importer").AsString() == StringView(u8"Script"));
    const String guid = imported.Get(u8"guid").AsString();

    // It landed in the source DB, in the requested group.
    JsonValue list = CallOk(server, u8"asset_list", Obj());
    CHECK(list.Get(u8"count").AsInt() == 1);
    CHECK(list.Get(u8"assets").At(0).Get(u8"group").AsString() == StringView(u8"scripts"));

    // The raw source was copied under Sources/ and the envelope written under Content/.
    CHECK(std::filesystem::exists("mcp_write_project/Sources/mcp_write_source.wren"));
    CHECK(std::filesystem::exists("mcp_write_project/Content/scripts/mcp_write_source.xasset"));

    // Cook builds it into the cooked DB (product guid == source guid).
    JsonValue cooked = CallOk(server, u8"asset_cook", Obj());
    CHECK(cooked.Get(u8"planned").AsInt() == 1);
    CHECK(cooked.Get(u8"cooked").AsInt() == 1);
    CHECK(cooked.Get(u8"failed").AsInt() == 0);
    CHECK(std::filesystem::exists("mcp_write_project/Cooked/scripts/mcp_write_source.rasset"));

    // The cooked product is now visible via asset_info on the cooked DB, under the source guid.
    JsonValue product = CallOk(server, u8"asset_info",
                               With(With(Obj(), u8"guid", guid.AsView()), u8"database", u8"cooked"));
    CHECK(product.Get(u8"guid").AsString() == guid);

    // A second cook is a no-op (nothing dirtied).
    JsonValue again = CallOk(server, u8"asset_cook", Obj());
    CHECK(again.Get(u8"planned").AsInt() == 0);
    CHECK(again.Get(u8"upToDate").AsInt() == 1);
}

TEST_CASE("integration.mcp: script_api reports the COMPLETE engine surface, headless, no device")
{
    // The surface-describing composition root: every subsystem facade, metadata only - no device,
    // no subsystem instantiated. This is the whole point of Fable's ruling A: the headless MCP host
    // reports the true engine script surface (RigidBody/Audio/Ui/SceneLoader/...), not just core.
    engine::RegisterAllScriptFacades();
    foundation::script::wren::RegisterWrenScriptBackend();

    McpServer server;
    RegisterScriptTools(server);

    JsonValue api = CallOk(server, u8"script_api", With(Obj(), u8"language", u8"wren"));
    JsonValue wren = api.Get(u8"languages").At(0);
    CHECK(wren.Get(u8"language").AsString() == StringView(u8"wren"));

    // Collect the bound script names.
    JsonValue types = wren.Get(u8"types");
    Array<String> names;
    for (i64 i = 0; i < types.Count(); ++i)
    {
        names.PushBack(types.At(i).Get(u8"scriptName").AsString());
    }
    const auto has = [&names](StringView n)
    {
        for (const String& s : names)
        {
            if (s.AsView() == n)
            {
                return true;
            }
        }
        return false;
    };

    // Core facades AND the gameplay-subsystem facades must all be present.
    CHECK(has(u8"Entity"));
    CHECK(has(u8"RigidBodyComponent")); // physics
    CHECK(has(u8"Audio"));              // audio
    CHECK(has(u8"Ui"));                 // game UI
    CHECK(has(u8"SceneLoader"));        // scene loader (GameInstance)
    CHECK(has(u8"Net"));                // networking
    // The full surface is much larger than the core-only 17 (physics/audio/input/ui/... added).
    CHECK(wren.Get(u8"typeCount").AsInt() > 30);
}

TEST_CASE("integration.mcp: project_info before any project is open is a tool error")
{
    McpServer server;
    editor::mcp::ProjectSession session;
    editor::mcp::RegisterProjectTools(server, session);

    JsonValue resp = CallResponse(server, u8"project_info", JsonValue::MakeObject());
    REQUIRE(resp.Has(u8"result"));
    CHECK(resp.Get(u8"result").Get(u8"isError").AsBool() == true);
}
