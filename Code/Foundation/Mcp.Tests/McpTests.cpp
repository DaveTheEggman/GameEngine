#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.json;
import foundation.mcp;

using namespace foundation::core;
using namespace foundation::mcp;
using foundation::json::JsonValue;
namespace json = foundation::json;

namespace
{
    // Parse a response line into a JsonValue (must be present + valid JSON).
    JsonValue Response(const Optional<String>& line)
    {
        REQUIRE(line.HasValue());
        json::ParseResult p = json::Parse(line.Value().AsView());
        REQUIRE(p.ok);
        return p.value;
    }

    bool Contains(StringView hay, StringView needle)
    {
        if (needle.Size() == 0 || needle.Size() > hay.Size())
        {
            return needle.Size() == 0;
        }
        for (usize i = 0; i + needle.Size() <= hay.Size(); ++i)
        {
            bool match = true;
            for (usize j = 0; j < needle.Size(); ++j)
            {
                if (hay.Data()[i + j] != needle.Data()[j])
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                return true;
            }
        }
        return false;
    }

    // A server with an echo tool (validated arg), an always-failing tool, and one resource.
    void Setup(McpServer& s)
    {
        s.RegisterTool(u8"echo", u8"Echoes the message back",
                       SchemaBuilder().Str(u8"message", u8"text to echo", true).Build(),
                       [](const JsonValue& args) -> ToolResult
                       {
                           JsonValue out = JsonValue::MakeObject();
                           out.Set(u8"echoed", JsonValue::MakeString(args.Get(u8"message").AsString()));
                           return out;
                       });
        s.RegisterTool(u8"fail", u8"Always fails", SchemaBuilder().Build(),
                       [](const JsonValue&) -> ToolResult
                       { return Err(String(u8"cook failed: bad input")); });
        s.RegisterResource(u8"mem://greeting", u8"greeting", u8"text/plain", u8"a greeting",
                           []() -> Result<String, String> { return String(u8"hello"); });
    }
}

// --- Lifecycle -------------------------------------------------------------

TEST_CASE("mcp: initialize returns the pinned version + {tools,resources} caps + serverInfo")
{
    McpServer s;
    Setup(s);
    JsonValue r = Response(s.HandleLine(
        u8"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}"));
    CHECK(r.Get(u8"jsonrpc").AsString() == StringView(u8"2.0"));
    CHECK(r.Get(u8"id").AsInt() == 1);
    const JsonValue res = r.Get(u8"result");
    CHECK(res.Get(u8"protocolVersion").AsString() == kProtocolVersion);
    const JsonValue caps = res.Get(u8"capabilities");
    CHECK(caps.Has(u8"tools"));
    CHECK(caps.Has(u8"resources"));
    CHECK_FALSE(caps.Has(u8"prompts")); // exactly {tools, resources}
    CHECK_FALSE(caps.Has(u8"sampling"));
    CHECK(res.Get(u8"serverInfo").Get(u8"name").IsString());
}

TEST_CASE("mcp: notifications never get a response (initialized + unknown/cancelled)")
{
    McpServer s;
    CHECK_FALSE(s.HandleLine(u8"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}").HasValue());
    CHECK_FALSE(
        s.HandleLine(u8"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{}}").HasValue());
    CHECK_FALSE(s.HandleLine(u8"{\"jsonrpc\":\"2.0\",\"method\":\"totally/unknown\"}").HasValue());
}

TEST_CASE("mcp: ping")
{
    McpServer s;
    JsonValue r = Response(s.HandleLine(u8"{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"method\":\"ping\"}"));
    CHECK(r.Has(u8"result"));
}

TEST_CASE("mcp: request id type (string vs number) is preserved verbatim")
{
    McpServer s;
    JsonValue rn = Response(s.HandleLine(u8"{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"ping\"}"));
    CHECK(rn.Get(u8"id").IsNumber());
    CHECK(rn.Get(u8"id").AsInt() == 42);
    JsonValue rs = Response(s.HandleLine(u8"{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"method\":\"ping\"}"));
    CHECK(rs.Get(u8"id").IsString());
    CHECK(rs.Get(u8"id").AsString() == StringView(u8"abc"));
}

// --- Tools -----------------------------------------------------------------

TEST_CASE("mcp: tools/list emits names, descriptions, and inputSchemas")
{
    McpServer s;
    Setup(s);
    JsonValue r = Response(s.HandleLine(u8"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"));
    const JsonValue tools = r.Get(u8"result").Get(u8"tools");
    CHECK(tools.Count() == 2);
    const JsonValue echo = tools.At(0);
    CHECK(echo.Get(u8"name").AsString() == StringView(u8"echo"));
    const JsonValue schema = echo.Get(u8"inputSchema");
    CHECK(schema.Get(u8"type").AsString() == StringView(u8"object"));
    CHECK(schema.Get(u8"properties").Has(u8"message"));
    CHECK(schema.Get(u8"required").At(0).AsString() == StringView(u8"message"));
}

TEST_CASE("mcp: tools/call round-trips through the registry (result is JSON-stringified text)")
{
    McpServer s;
    Setup(s);
    JsonValue r = Response(s.HandleLine(
        u8"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"echo\",\"arguments\":{\"message\":\"hi\"}}}"));
    const JsonValue res = r.Get(u8"result");
    CHECK(res.Get(u8"isError").AsBool() == false);
    const JsonValue content = res.Get(u8"content");
    CHECK(content.Count() == 1);
    CHECK(content.At(0).Get(u8"type").AsString() == StringView(u8"text"));
    const JsonValue payload = JsonValue::Parse(content.At(0).Get(u8"text").AsString());
    CHECK(payload.Get(u8"echoed").AsString() == StringView(u8"hi"));
}

TEST_CASE("mcp: a TOOL failure is a successful response with isError:true + the real text")
{
    McpServer s;
    Setup(s);
    JsonValue r = Response(s.HandleLine(
        u8"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"fail\"}}"));
    CHECK(r.Has(u8"result"));      // NOT a protocol error
    CHECK_FALSE(r.Has(u8"error"));
    CHECK(r.Get(u8"result").Get(u8"isError").AsBool() == true);
    CHECK(r.Get(u8"result").Get(u8"content").At(0).Get(u8"text").AsString() ==
          StringView(u8"cook failed: bad input"));
}

TEST_CASE("mcp: schema-validation failure is a PROTOCOL -32602 naming the field")
{
    McpServer s;
    Setup(s);
    // Missing required 'message'.
    JsonValue r = Response(s.HandleLine(
        u8"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"echo\",\"arguments\":{}}}"));
    CHECK(r.Has(u8"error"));
    CHECK(r.Get(u8"error").Get(u8"code").AsInt() == -32602);
    CHECK(Contains(r.Get(u8"error").Get(u8"message").AsString().AsView(), u8"message"));
    // Wrong type for 'message'.
    JsonValue r2 = Response(s.HandleLine(
        u8"{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"echo\",\"arguments\":{\"message\":5}}}"));
    CHECK(r2.Get(u8"error").Get(u8"code").AsInt() == -32602);
    CHECK(Contains(r2.Get(u8"error").Get(u8"message").AsString().AsView(), u8"message"));
    // Unknown tool -> -32602.
    JsonValue r3 = Response(s.HandleLine(
        u8"{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"nope\"}}"));
    CHECK(r3.Get(u8"error").Get(u8"code").AsInt() == -32602);
}

// --- Subset rejections -----------------------------------------------------

TEST_CASE("mcp: unknown method is -32601")
{
    McpServer s;
    JsonValue r = Response(s.HandleLine(u8"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"prompts/list\"}"));
    CHECK(r.Get(u8"error").Get(u8"code").AsInt() == -32601);
}

TEST_CASE("mcp: a top-level batch array is rejected -32600 (id null)")
{
    McpServer s;
    JsonValue r = Response(s.HandleLine(u8"[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}]"));
    CHECK(r.Get(u8"error").Get(u8"code").AsInt() == -32600);
    CHECK(r.Get(u8"id").IsNull());
}

// --- Resources -------------------------------------------------------------

TEST_CASE("mcp: resources/list + resources/read; unknown uri -> -32602")
{
    McpServer s;
    Setup(s);
    JsonValue l = Response(s.HandleLine(u8"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"resources/list\"}"));
    const JsonValue resources = l.Get(u8"result").Get(u8"resources");
    CHECK(resources.Count() == 1);
    CHECK(resources.At(0).Get(u8"uri").AsString() == StringView(u8"mem://greeting"));

    JsonValue rd = Response(s.HandleLine(
        u8"{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"resources/read\",\"params\":{\"uri\":\"mem://greeting\"}}"));
    CHECK(rd.Get(u8"result").Get(u8"contents").At(0).Get(u8"text").AsString() == StringView(u8"hello"));

    JsonValue miss = Response(s.HandleLine(
        u8"{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"resources/read\",\"params\":{\"uri\":\"mem://nope\"}}"));
    CHECK(miss.Get(u8"error").Get(u8"code").AsInt() == -32602);
}

TEST_CASE("mcp: a resource PROVIDER contributes a dynamic set (list + read + mime + miss)")
{
    McpServer s;
    Setup(s); // the static mem://greeting
    // A provider whose set changes between calls - exactly what static registration can't do.
    Array<String> names;
    names.PushBack(String(u8"alpha"));
    ResourceProvider provider;
    provider.list = [&names](Array<Resource>& out)
    {
        for (const String& n : names)
        {
            Resource r;
            r.uri = Format(u8"dyn://{}", n.AsView());
            r.name = n;
            r.mimeType = String(u8"application/xml");
            r.description = String(u8"dynamic entry");
            out.PushBack(Move(r));
        }
    };
    provider.read = [&names](StringView uri) -> Optional<Result<String, String>>
    {
        for (const String& n : names)
        {
            if (uri == Format(u8"dyn://{}", n.AsView()).AsView())
            {
                return Result<String, String>(Format(u8"content-of-{}", n.AsView()));
            }
        }
        return {}; // not ours
    };
    s.RegisterResourceProvider(Move(provider));

    // list = static + the provider's CURRENT entries.
    JsonValue l1 = Response(s.HandleLine(u8"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"resources/list\"}"));
    CHECK(l1.Get(u8"result").Get(u8"resources").Count() == 2);

    // The set grows without re-registration.
    names.PushBack(String(u8"beta"));
    JsonValue l2 = Response(s.HandleLine(u8"{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"resources/list\"}"));
    CHECK(l2.Get(u8"result").Get(u8"resources").Count() == 3);

    // Reads route through the provider and carry ITS declared mime type.
    JsonValue rd = Response(s.HandleLine(
        u8"{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"resources/read\",\"params\":{\"uri\":\"dyn://beta\"}}"));
    const JsonValue entry = rd.Get(u8"result").Get(u8"contents").At(0);
    CHECK(entry.Get(u8"text").AsString() == StringView(u8"content-of-beta"));
    CHECK(entry.Get(u8"mimeType").AsString() == StringView(u8"application/xml"));

    // Static reads still work, and a uri no one owns is still -32602.
    JsonValue st = Response(s.HandleLine(
        u8"{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"resources/read\",\"params\":{\"uri\":\"mem://greeting\"}}"));
    CHECK(st.Get(u8"result").Get(u8"contents").At(0).Get(u8"text").AsString() == StringView(u8"hello"));
    JsonValue miss = Response(s.HandleLine(
        u8"{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"resources/read\",\"params\":{\"uri\":\"dyn://gamma\"}}"));
    CHECK(miss.Get(u8"error").Get(u8"code").AsInt() == -32602);
}

// --- Framing / transport ---------------------------------------------------

TEST_CASE("mcp: framing survives garbage (-32700) and drives multiple messages; loop stays alive")
{
    McpServer s;
    Setup(s);
    InMemoryTransport t;
    t.Push(String(u8"this is not json"));                                        // -> -32700
    t.Push(String(u8"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}")); // -> nothing
    t.Push(String(u8"{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"ping\"}"));       // -> result
    Serve(s, t);

    REQUIRE(t.OutputCount() == 2); // the notification produced no line
    JsonValue e = json::Parse(t.Output(0).AsView()).value;
    CHECK(e.Get(u8"error").Get(u8"code").AsInt() == -32700);
    CHECK(e.Get(u8"id").IsNull());
    JsonValue p = json::Parse(t.Output(1).AsView()).value;
    CHECK(p.Get(u8"id").AsInt() == 7);
    CHECK(p.Has(u8"result"));
}
