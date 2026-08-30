// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Mcp.Script.Tests - script_api against a real backend.
//
// The tool is backend-neutral (it reads whatever the host registered), so the test registers the
// first available backend, then drives script_api over the server and asserts it reports that
// backend with a bound API. A second case narrows by language and checks the not-found tool error.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.json;
import foundation.mcp;
import foundation.mcp.script;
#if defined(OPTION_HAS_ANGELSCRIPT)
import foundation.script.angelscript;
#elif defined(OPTION_HAS_LUAU)
import foundation.script.luau;
#endif

using namespace foundation::core;
using namespace foundation::mcp;
using foundation::json::JsonValue;
namespace json = foundation::json;

namespace
{
    // Register the first available backend and return its language id, so these script_api tests
    // run against whichever single backend the build enabled (the tool is backend-neutral).
    [[nodiscard]] StringView RegisterSomeBackend()
    {
#if defined(OPTION_HAS_ANGELSCRIPT)
        foundation::script::angelscript::RegisterAngelScriptBackend();
        return StringView(u8"angelscript");
#else
        foundation::script::RegisterLuauScriptBackend();
        return StringView(u8"luau");
#endif
    }

    // Drive one tools/call and return the parsed tool-result payload (asserts isError == false).
    JsonValue CallOk(McpServer& s, StringView tool, JsonValue arguments)
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
        JsonValue resp = json::Parse(line.Value().AsView()).value;
        REQUIRE(resp.Has(u8"result"));
        CHECK(resp.Get(u8"result").Get(u8"isError").AsBool() == false);
        return JsonValue::Parse(resp.Get(u8"result").Get(u8"content").At(0).Get(u8"text").AsString());
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
        return json::Parse(s.HandleLine(req.ToString().AsView()).Value().AsView()).value;
    }
}

TEST_CASE("mcp.script: script_api reports the registered backend's bound API")
{
    const StringView backend = RegisterSomeBackend();

    McpServer server;
    RegisterScriptTools(server);

    JsonValue api = CallOk(server, u8"script_api", JsonValue::MakeObject());
    JsonValue languages = api.Get(u8"languages");
    REQUIRE(languages.Count() >= 1);

    // Find the registered backend among the reported languages and assert it bound a non-empty API.
    bool sawBackend = false;
    for (i64 i = 0; i < languages.Count(); ++i)
    {
        JsonValue lang = languages.At(i);
        if (lang.Get(u8"language").AsString() == backend)
        {
            sawBackend = true;
            CHECK(lang.Get(u8"typeCount").AsInt() > 0);
            CHECK(lang.Get(u8"types").Count() == lang.Get(u8"typeCount").AsInt());
            // Each type carries a script name and a (possibly empty) member list.
            JsonValue first = lang.Get(u8"types").At(0);
            CHECK(first.Get(u8"scriptName").AsString().Size() > 0u);
        }
    }
    CHECK(sawBackend);
}

TEST_CASE("mcp.script: script_api narrows by language, and an unknown language is a tool error")
{
    const StringView backend = RegisterSomeBackend();

    McpServer server;
    RegisterScriptTools(server);

    JsonValue narrowed = CallOk(server, u8"script_api",
                                [backend] {
                                    JsonValue a = JsonValue::MakeObject();
                                    a.Set(u8"language", JsonValue::MakeString(String(backend)));
                                    return a;
                                }());
    CHECK(narrowed.Get(u8"languages").Count() == 1);
    CHECK(narrowed.Get(u8"languages").At(0).Get(u8"language").AsString() == backend);

    JsonValue bogus = CallResponse(server, u8"script_api",
                                   [] {
                                       JsonValue a = JsonValue::MakeObject();
                                       a.Set(u8"language", JsonValue::MakeString(u8"cobol"));
                                       return a;
                                   }());
    CHECK(bogus.Get(u8"result").Get(u8"isError").AsBool() == true);
}
