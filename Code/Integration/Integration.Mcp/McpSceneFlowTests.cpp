// Integration.Mcp - the scene/prefab tool flow (mcp-agent-access.md P1 item 1): an agent opens a
// project, authors a scene THROUGH THE TOOLS (write validates first), validates it, reads it back
// byte-identically, and gets real refusals with reasons (garbage XML, wrong-type guid redirects,
// multi-root prefabs). The seed XML comes from a REAL SaveScene, so the tools are proven against
// the exact text the editor writes.
#include <doctest/doctest.h>
#include <filesystem>
#include "Core/Prelude.h"
import foundation.core;
import foundation.json;
import foundation.content;
import foundation.vfs;
import foundation.scene;
import foundation.scene.resource;
import foundation.net.replication;
import foundation.mcp;
import engine.scenesurface;
import editor.core;
import editor.mcp;

using namespace foundation::core;
using namespace foundation::mcp;
namespace json = foundation::json;
using foundation::json::JsonValue;
namespace scene = foundation::scene;

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
        return JsonValue::Parse(
            resp.Get(u8"result").Get(u8"content").At(0).Get(u8"text").AsString());
    }

    String CallErr(McpServer& s, StringView tool, JsonValue arguments)
    {
        JsonValue resp = CallResponse(s, tool, Move(arguments));
        REQUIRE(resp.Has(u8"result"));
        CHECK(resp.Get(u8"result").Get(u8"isError").AsBool() == true);
        return String(resp.Get(u8"result").Get(u8"content").At(0).Get(u8"text").AsString());
    }

    // Raw JSON-RPC line -> response line (for the resources/* methods, which are not tools).
    String Response(McpServer& s, StringView line)
    {
        Optional<String> out = s.HandleLine(line);
        REQUIRE(out.HasValue());
        return Move(out.Value());
    }

    JsonValue Obj() { return JsonValue::MakeObject(); }
    JsonValue With(JsonValue o, StringView k, StringView v)
    {
        o.Set(String(k), JsonValue::MakeString(String(v)));
        return o;
    }

    // Every occurrence of `from` in `text` replaced with `to` (byte-wise; test-local helper).
    String ReplaceAll(StringView text, StringView from, StringView to)
    {
        StringBuilder out;
        usize i = 0;
        while (i < text.Size())
        {
            if (i + from.Size() <= text.Size() && text.SubStr(i, from.Size()) == from)
            {
                out.Append(to);
                i += from.Size();
                continue;
            }
            out.Append(text[i]);
            ++i;
        }
        return out.Take();
    }
}

TEST_CASE("integration.mcp: scene tools - author, validate, read back, and real refusals")
{
    std::error_code ec;
    std::filesystem::remove_all("mcp_scene_project", ec);

    McpServer server;
    editor::mcp::ProjectSession session;
    editor::mcp::RegisterProjectTools(server, session);
    editor::mcp::RegisterSceneTools(server, session);
    editor::mcp::RegisterProjectResources(server, session); // scenes as project:// resources

    (void)CallOk(server, u8"project_create",
                 With(With(Obj(), u8"directory", u8"mcp_scene_project"), u8"name", u8"SceneFix"));
    (void)CallOk(server, u8"project_open", With(Obj(), u8"directory", u8"mcp_scene_project"));

    // Seed XML from a REAL SaveScene (the exact editor-written text): two entities, one child,
    // and a REAL component record (NetworkComponent on hero) - the full manager set is on the
    // authored scene exactly as it is on the validate scratch, so validation exercises a genuine
    // component payload end to end.
    engine::RegisterAllSceneComponentReflection(); // as the MCP host does at startup
    String seedXml;
    String seedGuid;
    {
        scene::Scene authored(u8"arena");
        engine::AddAllSceneManagers(authored);
        scene::EntityHandle hero = authored.CreateEntity(u8"hero");
        scene::EntityHandle torch = authored.CreateEntity(u8"torch");
        authored.SetParent(torch, hero);
        authored.GetSystem<foundation::net::NetworkComponentManager>()->Add(hero);

        auto* inst = session.project->SourceDb().RootGroup()->CreateInstance(
            u8"seed", scene::SceneDocument::StaticType());
        REQUIRE(inst != nullptr);
        REQUIRE(scene::SaveScene(authored, *inst).IsOk());
        utf8char guidChars[37];
        inst->Id().ToChars(guidChars);
        seedGuid = String(StringView(guidChars, 36));
    }

    // scene_read returns the stored text.
    JsonValue read = CallOk(server, u8"scene_read", With(Obj(), u8"guid", seedGuid.AsView()));
    CHECK(read.Get(u8"name").AsString() == StringView(u8"seed"));
    seedXml = read.Get(u8"xml").AsString();
    CHECK(seedXml.Size() > 0u);

    // scene_validate (raw xml) confirms structure AND the component payload: validation is FULL
    // (the scratch carries the complete engine manager set), so the NetworkComponent record
    // parses through its real manager with zero warnings.
    JsonValue valid = CallOk(server, u8"scene_validate", With(Obj(), u8"xml", seedXml.AsView()));
    CHECK(valid.Get(u8"valid").AsBool() == true);
    CHECK(valid.Get(u8"sceneName").AsString() == StringView(u8"arena"));
    CHECK(valid.Get(u8"entityCount").AsNumber() == doctest::Approx(2.0));
    CHECK(valid.Get(u8"rootCount").AsNumber() == doctest::Approx(1.0));
    CHECK(valid.Get(u8"componentValidation").AsString() == StringView(u8"full"));
    CHECK(valid.Get(u8"warnings").Count() == 0);

    // A record of a GENUINELY unknown component type is skipped with a captured warning (still
    // valid: the reader's contract is skip-and-warn, and the agent is told what was skipped).
    {
        const String mangled =
            ReplaceAll(seedXml.AsView(), u8"net.Network", u8"bogus.NoSuchComponent");
        JsonValue report =
            CallOk(server, u8"scene_validate", With(Obj(), u8"xml", mangled.AsView()));
        CHECK(report.Get(u8"valid").AsBool() == true);
        CHECK(report.Get(u8"warnings").Count() >= 1);
    }

    // scene_write CREATES a new scene from the XML; the stored stream is byte-identical.
    JsonValue written = CallOk(
        server, u8"scene_write",
        With(With(With(Obj(), u8"xml", seedXml.AsView()), u8"name", u8"authored"), u8"group",
             u8"Levels"));
    CHECK(written.Get(u8"written").AsBool() == true);
    const String newGuid(written.Get(u8"guid").AsString());
    JsonValue readBack = CallOk(server, u8"scene_read", With(Obj(), u8"guid", newGuid.AsView()));
    CHECK(readBack.Get(u8"xml").AsString() == seedXml.AsView()); // verbatim storage

    // scene_validate by guid works on the stored stream too.
    JsonValue storedValid =
        CallOk(server, u8"scene_validate", With(Obj(), u8"guid", newGuid.AsView()));
    CHECK(storedValid.Get(u8"valid").AsBool() == true);

    // REFUSALS carry reasons:
    // (a) garbage XML never writes.
    const String garbage = CallErr(
        server, u8"scene_write",
        With(With(Obj(), u8"xml", u8"<not a scene>"), u8"name", u8"broken"));
    CHECK(garbage.Size() > 0u);
    // (b) a scene guid through the prefab tool redirects.
    const String redirect =
        CallErr(server, u8"prefab_read", With(Obj(), u8"guid", newGuid.AsView()));
    CHECK(redirect.Size() > 0u);
    // (c) a SINGLE-root scene is a legal prefab (writes fine)...
    JsonValue okPrefab = CallOk(
        server, u8"prefab_write",
        With(With(Obj(), u8"xml", seedXml.AsView()), u8"name", u8"goodprefab"));
    CHECK(okPrefab.Get(u8"written").AsBool() == true);
    // ...and a MULTI-root one is refused with the single-root rule.
    {
        scene::Scene twoRoots(u8"pair");
        (void)twoRoots.CreateEntity(u8"a");
        (void)twoRoots.CreateEntity(u8"b");
        auto* inst = session.project->SourceDb().RootGroup()->CreateInstance(
            u8"pairseed", scene::SceneDocument::StaticType());
        REQUIRE(scene::SaveScene(twoRoots, *inst).IsOk());
        utf8char guidChars[37];
        inst->Id().ToChars(guidChars);
        JsonValue pairRead = CallOk(server, u8"scene_read",
                                    With(Obj(), u8"guid", StringView(guidChars, 36)));
        const String pairXml(pairRead.Get(u8"xml").AsString());
        const String refused = CallErr(
            server, u8"prefab_write",
            With(With(Obj(), u8"xml", pairXml.AsView()), u8"name", u8"badprefab2"));
        CHECK(refused.Size() > 0u);
    }

    // scene_validate demands exactly one input.
    (void)CallErr(server, u8"scene_validate", Obj());
    (void)CallErr(server, u8"scene_validate",
                  With(With(Obj(), u8"xml", u8"x"), u8"guid", newGuid.AsView()));

    // The scenes double as READ-ONLY resources (project://scene/<guid>), listed LIVE from the
    // source DB - the written scene appears without any re-registration, and its content is
    // byte-identical to scene_read.
    {
        const String listLine =
            Response(server, u8"{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"resources/list\"}");
        JsonValue list = json::Parse(listLine.AsView()).value;
        const JsonValue resources = list.Get(u8"result").Get(u8"resources");
        const String wantedUri = Format(u8"project://scene/{}", newGuid.AsView());
        bool found = false;
        for (i64 i = 0; i < resources.Count(); ++i)
        {
            if (resources.At(i).Get(u8"uri").AsString() == wantedUri.AsView())
            {
                found = true;
                CHECK(resources.At(i).Get(u8"mimeType").AsString() ==
                      StringView(u8"application/xml"));
            }
        }
        CHECK(found);

        const String readLine = Response(
            server,
            Format(u8"{}{}{}",
                   StringView(u8"{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"resources/read\","
                              u8"\"params\":{\"uri\":\""),
                   wantedUri.AsView(), StringView(u8"\"}}"))
                .AsView());
        JsonValue read2 = json::Parse(readLine.AsView()).value;
        CHECK(read2.Get(u8"result").Get(u8"contents").At(0).Get(u8"text").AsString() ==
              seedXml.AsView());
    }
}

TEST_CASE("integration.mcp: host_info reports pid, stamp, versions, and host state")
{
    McpServer server;
    server.SetServerInfo(u8"test-host", u8"9.9.9");
    bool open = false;
    RegisterHostInfoTool(server, String(u8"stamp-abc123"),
                         Function<JsonValue()>{
                             [&open]()
                             {
                                 JsonValue host = JsonValue::MakeObject();
                                 host.Set(u8"projectOpen", JsonValue::MakeBool(open));
                                 return host;
                             }});

    JsonValue info = CallOk(server, u8"host_info", Obj());
    CHECK(info.Get(u8"pid").AsNumber() > 0.0);
    CHECK(info.Get(u8"buildStamp").AsString() == StringView(u8"stamp-abc123"));
    CHECK(info.Get(u8"serverName").AsString() == StringView(u8"test-host"));
    CHECK(info.Get(u8"serverVersion").AsString() == StringView(u8"9.9.9"));
    CHECK(info.Get(u8"protocolVersion").AsString().Size() > 0u);
    CHECK(info.Get(u8"host").Get(u8"projectOpen").AsBool() == false);

    // The host-state lambda is LIVE, not captured-at-registration.
    open = true;
    JsonValue after = CallOk(server, u8"host_info", Obj());
    CHECK(after.Get(u8"host").Get(u8"projectOpen").AsBool() == true);
}
