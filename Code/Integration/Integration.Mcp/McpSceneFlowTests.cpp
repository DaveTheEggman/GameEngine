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
import foundation.mcp;
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

    JsonValue Obj() { return JsonValue::MakeObject(); }
    JsonValue With(JsonValue o, StringView k, StringView v)
    {
        o.Set(String(k), JsonValue::MakeString(String(v)));
        return o;
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

    (void)CallOk(server, u8"project_create",
                 With(With(Obj(), u8"directory", u8"mcp_scene_project"), u8"name", u8"SceneFix"));
    (void)CallOk(server, u8"project_open", With(Obj(), u8"directory", u8"mcp_scene_project"));

    // Seed XML from a REAL SaveScene (the exact editor-written text): two entities, one child.
    String seedXml;
    String seedGuid;
    {
        scene::Scene authored(u8"arena");
        scene::EntityHandle hero = authored.CreateEntity(u8"hero");
        scene::EntityHandle torch = authored.CreateEntity(u8"torch");
        authored.SetParent(torch, hero);

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

    // scene_validate (raw xml) confirms structure.
    JsonValue valid = CallOk(server, u8"scene_validate", With(Obj(), u8"xml", seedXml.AsView()));
    CHECK(valid.Get(u8"valid").AsBool() == true);
    CHECK(valid.Get(u8"sceneName").AsString() == StringView(u8"arena"));
    CHECK(valid.Get(u8"entityCount").AsNumber() == doctest::Approx(2.0));
    CHECK(valid.Get(u8"rootCount").AsNumber() == doctest::Approx(1.0));

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
