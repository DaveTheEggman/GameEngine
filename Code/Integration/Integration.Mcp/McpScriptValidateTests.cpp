// Integration.Mcp - script_validate (mcp-agent-access.md P1 item 7, compile-check form). The
// strongest cheap proof: every enabled backend's OWN New-Asset starter must validate through
// the tool with its metadata harvested (class name + handlers recognized), and broken source
// must come back with real line-numbered compile errors.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import foundation.core;
import foundation.json;
import foundation.mcp;
import pipeline.core;
import pipeline.registration;
import script.pipeline;
import editor.mcp;

using namespace foundation::core;
using namespace foundation::mcp;
namespace json = foundation::json;
using foundation::json::JsonValue;

namespace
{
    JsonValue SvCall(McpServer& s, JsonValue arguments)
    {
        JsonValue params = JsonValue::MakeObject();
        params.Set(u8"name", JsonValue::MakeString(u8"script_validate"));
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

    JsonValue SvOk(McpServer& s, JsonValue arguments)
    {
        JsonValue resp = SvCall(s, Move(arguments));
        REQUIRE(resp.Has(u8"result"));
        CHECK(resp.Get(u8"result").Get(u8"isError").AsBool() == false);
        return JsonValue::Parse(
            resp.Get(u8"result").Get(u8"content").At(0).Get(u8"text").AsString());
    }

    JsonValue SvArgs(StringView language, StringView source)
    {
        JsonValue a = JsonValue::MakeObject();
        a.Set(u8"language", JsonValue::MakeString(String(language)));
        a.Set(u8"source", JsonValue::MakeString(String(source)));
        return a;
    }

    bool HasHandler(const JsonValue& handlers, StringView name)
    {
        for (i64 i = 0; i < handlers.Count(); ++i)
        {
            if (handlers.At(i).AsString() == name)
            {
                return true;
            }
        }
        return false;
    }
}

TEST_CASE("integration.mcp: script_validate - every enabled backend's starter compiles; "
          "broken source reports lines")
{
    pipeline::RegisterPipelineTypes(); // registers the enabled language cooks (idempotent)

    McpServer server;
    editor::mcp::RegisterScriptValidateTool(server);

    const StringView languages[] = {u8"wren", u8"angelscript", u8"luau"};
    usize enabled = 0;
    for (StringView language : languages)
    {
        pipeline::IScriptLanguageCook* cook =
            pipeline::ScriptLanguageCookRegistry::Get().FindByLanguage(language);
        if (cook == nullptr)
        {
            continue; // backend disabled in this build config
        }
        ++enabled;

        // The backend's own Behavior starter validates, and the harvest recognized it.
        const StringView starter = cook->NewAssetTemplate(pipeline::ScriptTier::Behavior);
        JsonValue ok = SvOk(server, SvArgs(language, starter));
        CHECK(ok.Get(u8"valid").AsBool() == true);
        CHECK(ok.Get(u8"errors").Count() == 0);
        CHECK(ok.Get(u8"className").AsString() == StringView(u8"NewBehavior"));
        CHECK(HasHandler(ok.Get(u8"handlers"), u8"onUpdate"));
        CHECK(ok.Get(u8"checkLevel").AsString() == StringView(u8"compile"));

        // Broken source fails with at least one line-numbered error.
        JsonValue bad =
            SvOk(server, SvArgs(language, u8"this is ( not : a valid { script"));
        CHECK(bad.Get(u8"valid").AsBool() == false);
        REQUIRE(bad.Get(u8"errors").Count() >= 1);
        CHECK(bad.Get(u8"errors").At(0).Get(u8"line").AsNumber() >= 1.0);
        CHECK(String(bad.Get(u8"errors").At(0).Get(u8"message").AsString()).Size() > 0u);
    }
    CHECK(enabled >= 1); // the suite must actually exercise at least one backend

    // A missing required arg is a PROTOCOL error (schema validation), not a tool error.
    JsonValue noSource = JsonValue::MakeObject();
    noSource.Set(u8"language", JsonValue::MakeString(u8"luau"));
    JsonValue params = JsonValue::MakeObject();
    params.Set(u8"name", JsonValue::MakeString(u8"script_validate"));
    params.Set(u8"arguments", Move(noSource));
    JsonValue req = JsonValue::MakeObject();
    req.Set(u8"jsonrpc", JsonValue::MakeString(u8"2.0"));
    req.Set(u8"id", JsonValue::MakeNumber(2));
    req.Set(u8"method", JsonValue::MakeString(u8"tools/call"));
    req.Set(u8"params", Move(params));
    Optional<String> line = server.HandleLine(req.ToString().AsView());
    REQUIRE(line.HasValue());
    JsonValue resp = json::Parse(line.Value().AsView()).value;
    CHECK(resp.Get(u8"error").Get(u8"code").AsInt() == -32602);
}
