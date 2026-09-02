// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Foundation::Mcp.Http tests - the streamable-HTTP binding over REAL loopback sockets: an
// authorized JSON-RPC exchange (initialize + a tool call) through HttpFetch, every refusal
// (no token, wrong token, wrong method, unknown endpoint), the 202 notification path, and
// the SSE event channel end to end (connect, broadcast, receive, disconnect sweep).
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.net;
import foundation.http;
import foundation.json;
import foundation.mcp;
import foundation.mcp.http;

using namespace foundation::core;
using namespace foundation::http;
using namespace foundation::mcp;
namespace json = foundation::json;
using foundation::json::JsonValue;
namespace net = foundation::net;

namespace
{
    HttpRequest Post(StringView token, StringView body)
    {
        HttpRequest r;
        r.method = String(u8"POST");
        r.target = String(u8"/mcp");
        if (!token.IsEmpty())
        {
            r.headers.PushBack(
                HttpHeader{String(u8"Authorization"), Format(u8"Bearer {}", token)});
        }
        r.body.Resize(body.Size());
        for (usize i = 0; i < body.Size(); ++i)
        {
            r.body[i] = static_cast<byte>(body[i]);
        }
        return r;
    }

    void PumpUntil(McpHttpHost& host, const bool& done)
    {
        for (u32 i = 0; i < 5000 && !done; ++i)
        {
            if (host.Pump() == 0)
            {
                SleepMilliseconds(1);
            }
        }
    }
}

TEST_CASE("mcp.http: an McpServer over HTTP - the exchange, every refusal, and 202")
{
    McpServer server;
    server.SetServerInfo(u8"http-host", u8"1.0.0");
    server.RegisterTool(u8"ping_tool", u8"answers pong", SchemaBuilder().Build(),
                        [](const JsonValue&) -> ToolResult
                        {
                            JsonValue out = JsonValue::MakeObject();
                            out.Set(u8"pong", JsonValue::MakeBool(true));
                            return out;
                        });

    McpHttpHost host(DefaultAllocator(), server);
    CHECK(!host.Start(McpHttpConfig{0, String()})); // an empty token never serves
    REQUIRE(host.Start(McpHttpConfig{0, String(u8"sekrit")}));
    const u16 port = host.BoundPort();
    REQUIRE(port != 0);

    struct Outcome
    {
        Result<HttpResponse, String> init = Err(String(u8"unset"));
        Result<HttpResponse, String> call = Err(String(u8"unset"));
        Result<HttpResponse, String> noToken = Err(String(u8"unset"));
        Result<HttpResponse, String> badToken = Err(String(u8"unset"));
        Result<HttpResponse, String> badMethod = Err(String(u8"unset"));
        Result<HttpResponse, String> badPath = Err(String(u8"unset"));
        Result<HttpResponse, String> note = Err(String(u8"unset"));
    } outcome;
    bool done = false;
    Thread client(
        [&]
        {
            outcome.init = HttpFetch(
                u8"127.0.0.1", port,
                Post(u8"sekrit",
                     u8"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":"
                     u8"{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},"
                     u8"\"clientInfo\":{\"name\":\"t\",\"version\":\"0\"}}}"));
            outcome.call = HttpFetch(
                u8"127.0.0.1", port,
                Post(u8"sekrit", u8"{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\","
                                 u8"\"params\":{\"name\":\"ping_tool\",\"arguments\":{}}}"));
            outcome.noToken = HttpFetch(u8"127.0.0.1", port, Post({}, u8"{}"));
            outcome.badToken = HttpFetch(u8"127.0.0.1", port, Post(u8"wrong", u8"{}"));
            HttpRequest getMcp = Post(u8"sekrit", u8"");
            getMcp.method = String(u8"GET");
            outcome.badMethod = HttpFetch(u8"127.0.0.1", port, getMcp);
            HttpRequest elsewhere = Post(u8"sekrit", u8"{}");
            elsewhere.target = String(u8"/nope");
            outcome.badPath = HttpFetch(u8"127.0.0.1", port, elsewhere);
            // A notification (no id) has no JSON-RPC response - the transport answers 202.
            outcome.note = HttpFetch(
                u8"127.0.0.1", port,
                Post(u8"sekrit",
                     u8"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}"));
            done = true;
        });
    PumpUntil(host, done);
    client.Join();

    REQUIRE(outcome.init.HasValue());
    CHECK(outcome.init.Value().status == 200);
    {
        JsonValue r = json::Parse(outcome.init.Value().BodyText()).value;
        CHECK(r.Get(u8"result").Get(u8"serverInfo").Get(u8"name").AsString() ==
              StringView(u8"http-host"));
    }
    REQUIRE(outcome.call.HasValue());
    CHECK(outcome.call.Value().status == 200);
    {
        JsonValue r = json::Parse(outcome.call.Value().BodyText()).value;
        CHECK(r.Get(u8"result").Get(u8"isError").AsBool() == false);
    }
    REQUIRE(outcome.noToken.HasValue());
    CHECK(outcome.noToken.Value().status == 401);
    REQUIRE(outcome.badToken.HasValue());
    CHECK(outcome.badToken.Value().status == 401);
    REQUIRE(outcome.badMethod.HasValue());
    CHECK(outcome.badMethod.Value().status == 405);
    REQUIRE(outcome.badPath.HasValue());
    CHECK(outcome.badPath.Value().status == 404);
    REQUIRE(outcome.note.HasValue());
    CHECK(outcome.note.Value().status == 202);
}

TEST_CASE("mcp.http: the SSE event channel - connect, broadcast, receive, sweep")
{
    McpServer server;
    McpHttpHost host(DefaultAllocator(), server);
    REQUIRE(host.Start(McpHttpConfig{0, String(u8"sekrit")}));
    const u16 port = host.BoundPort();

    String received;
    Mutex receivedMutex;
    bool clientDone = false;
    Thread listener(
        [&]
        {
            net::TcpSocket raw = net::TcpSocket::Connect(u8"127.0.0.1", port);
            for (u32 i = 0; i < 5000 && raw.ConnectStatus() == 0; ++i)
            {
                SleepMilliseconds(1);
            }
            const StringView get = u8"GET /events HTTP/1.1\r\nHost: local\r\n"
                                   u8"Authorization: Bearer sekrit\r\n\r\n";
            (void)raw.Send(Span<const byte>(reinterpret_cast<const byte*>(get.Data()),
                                            get.Size()));
            byte buffer[2048];
            for (u32 i = 0; i < 5000; ++i)
            {
                const i64 n = raw.Receive(Span<byte>(buffer, sizeof(buffer)));
                if (n > 0)
                {
                    ScopedLock lock(receivedMutex);
                    received.Append(StringView(reinterpret_cast<const utf8char*>(buffer),
                                               static_cast<usize>(n)));
                    bool sawEvent = false;
                    for (usize c = 0; c + 14 < received.Size(); ++c)
                    {
                        if (received.AsView().SubStr(c, 15) ==
                            StringView(u8"data: cook done"))
                        {
                            sawEvent = true;
                        }
                    }
                    if (sawEvent)
                    {
                        break;
                    }
                    continue;
                }
                if (n == 0)
                {
                    SleepMilliseconds(1);
                    continue;
                }
                break;
            }
            clientDone = true;
        });

    // Pump until the listener registers, then broadcast.
    for (u32 i = 0; i < 5000 && host.ListenerCount() == 0; ++i)
    {
        if (host.Pump() == 0)
        {
            SleepMilliseconds(1);
        }
    }
    REQUIRE(host.ListenerCount() == 1);
    CHECK(host.Broadcast(u8"progress", u8"cook done") == 1u);
    for (u32 i = 0; i < 5000 && !clientDone; ++i)
    {
        (void)host.Pump();
        SleepMilliseconds(1);
    }
    listener.Join();

    ScopedLock lock(receivedMutex);
    const StringView text = received.AsView();
    CHECK(text.StartsWith(u8"HTTP/1.1 200 OK"));
    bool sawConnected = false;
    bool sawEvent = false;
    for (usize i = 0; i < text.Size(); ++i)
    {
        if (text.SubStr(i, text.Size() - i).StartsWith(u8": connected"))
        {
            sawConnected = true;
        }
        if (text.SubStr(i, text.Size() - i).StartsWith(u8"event: progress"))
        {
            sawEvent = true;
        }
    }
    CHECK(sawConnected);
    CHECK(sawEvent);

    // The peer is gone; the liveness-polling sweep drops the listener within a few pumps
    // (a WRITE alone cannot detect a fresh close - TCP buffers it until the reset arrives).
    for (u32 i = 0; i < 2000 && host.ListenerCount() != 0; ++i)
    {
        (void)host.Pump();
        SleepMilliseconds(1);
    }
    CHECK(host.ListenerCount() == 0);
    CHECK(host.Broadcast(u8"progress", u8"anyone?") == 0u);
}
