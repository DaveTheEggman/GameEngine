// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Foundation::Http tests - the parser edges (split pushes, case-insensitive headers, limits,
// close-delimited bodies) and REAL loopback exchanges: a pumped server on an OS-assigned port,
// the blocking client from a worker thread, one-shot requests, refusals, and an SSE stream
// read raw off the socket.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.net;
import foundation.http;

using namespace foundation::core;
using namespace foundation::http;
namespace net = foundation::net;

namespace
{
    Span<const byte> Bytes(StringView text)
    {
        return Span<const byte>(reinterpret_cast<const byte*>(text.Data()), text.Size());
    }

    // Pump `server` until `done` flips or ~5 s passes (the test-side serve loop).
    void PumpUntil(HttpServer& server, const bool& done)
    {
        for (u32 i = 0; i < 5000 && !done; ++i)
        {
            if (server.Pump() == 0)
            {
                SleepMilliseconds(1);
            }
        }
    }
}

TEST_CASE("http: parser - a request in arbitrarily split pushes")
{
    const StringView wire = u8"POST /mcp?x=1 HTTP/1.1\r\n"
                            u8"Host: local\r\n"
                            u8"content-type: application/json\r\n"
                            u8"Content-Length: 11\r\n"
                            u8"\r\n"
                            u8"{\"ok\":true}";
    // Push one byte at a time - the harshest framing.
    HttpMessageParser parser(HttpMessageParser::Mode::Request);
    HttpParseState state = HttpParseState::NeedMore;
    for (usize i = 0; i < wire.Size(); ++i)
    {
        state = parser.Push(Bytes(wire.SubStr(i, 1)));
    }
    REQUIRE(state == HttpParseState::Complete);
    CHECK(parser.Method() == StringView(u8"POST"));
    CHECK(parser.Target() == StringView(u8"/mcp?x=1"));
    // Case-insensitive lookup finds the lower-case spelling.
    CHECK(FindHeader(parser.Headers(), u8"Content-Type") ==
          StringView(u8"application/json"));
    CHECK(parser.Body().Size() == 11u);
}

TEST_CASE("http: parser - failures (garbage, chunked, oversized head, body cap)")
{
    {
        HttpMessageParser p(HttpMessageParser::Mode::Request);
        CHECK(p.Push(Bytes(u8"this is not http\r\n\r\n")) == HttpParseState::Failed);
    }
    {
        HttpMessageParser p(HttpMessageParser::Mode::Request);
        CHECK(p.Push(Bytes(u8"POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n")) ==
              HttpParseState::Failed);
    }
    {
        HttpMessageParser p(HttpMessageParser::Mode::Request);
        String huge(u8"GET / HTTP/1.1\r\nX: ");
        for (usize i = 0; i < 20000; ++i)
        {
            huge.PushBack(u8'a');
        }
        CHECK(p.Push(Bytes(huge.AsView())) == HttpParseState::Failed);
    }
    {
        HttpMessageParser p(HttpMessageParser::Mode::Request, /*maxBodyBytes=*/8);
        CHECK(p.Push(Bytes(u8"POST / HTTP/1.1\r\nContent-Length: 9\r\n\r\n")) ==
              HttpParseState::Failed);
    }
}

TEST_CASE("http: parser - close-delimited response body")
{
    HttpMessageParser p(HttpMessageParser::Mode::Response);
    CHECK(p.Push(Bytes(u8"HTTP/1.1 200 OK\r\nX-Kind: raw\r\n\r\nhello")) ==
          HttpParseState::NeedMore);
    CHECK(p.Push(Bytes(u8" world")) == HttpParseState::NeedMore);
    CHECK(p.OnPeerClosed() == HttpParseState::Complete);
    CHECK(p.Status() == 200);
    CHECK(p.Body().Size() == 11u);
}

TEST_CASE("http: loopback - one-shot request/response, 404, and bad request")
{
    HttpServer server;
    REQUIRE(server.Start(HttpServerConfig{}));
    const u16 port = server.BoundPort();
    REQUIRE(port != 0);
    server.SetHandler(
        [](const HttpRequest& request) -> HttpResponse
        {
            if (request.target.AsView() == StringView(u8"/echo"))
            {
                return HttpResponse::Json(
                    200, Format(u8"{{\"method\":\"{}\",\"size\":{}}}",
                                request.method.AsView(), request.body.Size())
                             .AsView());
            }
            return HttpResponse::Text(404, u8"text/plain", u8"not here");
        });

    bool done = false;
    // Client on a worker; the test thread pumps the server.
    struct Outcome
    {
        Result<HttpResponse, String> echo = Err(String(u8"unset"));
        Result<HttpResponse, String> missing = Err(String(u8"unset"));
    } outcome;
    Thread client(
        [&]
        {
            HttpRequest post;
            post.method = String(u8"POST");
            post.target = String(u8"/echo");
            post.headers.PushBack(
                HttpHeader{String(u8"Content-Type"), String(u8"application/json")});
            for (usize i = 0; i < 5; ++i)
            {
                post.body.PushBack(byte{'x'});
            }
            outcome.echo = HttpFetch(u8"127.0.0.1", port, post);
            HttpRequest get;
            get.method = String(u8"GET");
            get.target = String(u8"/nope");
            outcome.missing = HttpFetch(u8"127.0.0.1", port, get);
            done = true;
        });
    PumpUntil(server, done);
    client.Join();

    REQUIRE(outcome.echo.HasValue());
    CHECK(outcome.echo.Value().status == 200);
    CHECK(outcome.echo.Value().Header(u8"content-type") ==
          StringView(u8"application/json"));
    const StringView echoBody(
        reinterpret_cast<const utf8char*>(outcome.echo.Value().body.Data()),
        outcome.echo.Value().body.Size());
    CHECK(echoBody == StringView(u8"{\"method\":\"POST\",\"size\":5}"));
    REQUIRE(outcome.missing.HasValue());
    CHECK(outcome.missing.Value().status == 404);

    // Raw garbage answers 400 and the server SURVIVES.
    bool done2 = false;
    bool got400 = false;
    Thread rawClient(
        [&]
        {
            net::TcpSocket raw = net::TcpSocket::Connect(u8"127.0.0.1", port);
            for (u32 i = 0; i < 5000 && raw.ConnectStatus() == 0; ++i)
            {
                SleepMilliseconds(1);
            }
            const StringView garbage = u8"complete garbage\r\n\r\n";
            (void)raw.Send(Bytes(garbage));
            HttpMessageParser p(HttpMessageParser::Mode::Response);
            byte buffer[1024];
            for (u32 i = 0; i < 5000; ++i)
            {
                const i64 n = raw.Receive(Span<byte>(buffer, sizeof(buffer)));
                if (n > 0)
                {
                    if (p.Push(Span<const byte>(buffer, static_cast<usize>(n))) ==
                        HttpParseState::Complete)
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
                (void)p.OnPeerClosed();
                break;
            }
            got400 = p.Status() == 400;
            done2 = true;
        });
    PumpUntil(server, done2);
    rawClient.Join();
    CHECK(got400);

    // And a normal request still works after the garbage one.
    bool done3 = false;
    Result<HttpResponse, String> after = Err(String(u8"unset"));
    Thread client3(
        [&]
        {
            HttpRequest get;
            get.method = String(u8"GET");
            get.target = String(u8"/echo");
            after = HttpFetch(u8"127.0.0.1", port, get);
            done3 = true;
        });
    PumpUntil(server, done3);
    client3.Join();
    REQUIRE(after.HasValue());
    CHECK(after.Value().status == 200);
}

TEST_CASE("http: loopback - a Server-Sent Events stream delivers events as they are written")
{
    HttpServer server;
    REQUIRE(server.Start(HttpServerConfig{}));
    const u16 port = server.BoundPort();
    server.SetHandler([](const HttpRequest&) { return HttpResponse::EventStream(); });
    RefPtr<SseStream> held;
    server.SetStreamHandler([&held](const HttpRequest&, RefPtr<SseStream> stream)
                            { held = Move(stream); });

    // A raw client: send the GET, then read whatever the server pushes.
    String received;
    bool clientDone = false;
    bool sawHeaders = false;
    Mutex receivedMutex;
    Thread client(
        [&]
        {
            net::TcpSocket raw = net::TcpSocket::Connect(u8"127.0.0.1", port);
            for (u32 i = 0; i < 5000 && raw.ConnectStatus() == 0; ++i)
            {
                SleepMilliseconds(1);
            }
            const StringView get = u8"GET /events HTTP/1.1\r\nHost: local\r\n\r\n";
            (void)raw.Send(Bytes(get));
            byte buffer[2048];
            for (u32 i = 0; i < 5000; ++i)
            {
                const i64 n = raw.Receive(Span<byte>(buffer, sizeof(buffer)));
                if (n > 0)
                {
                    ScopedLock lock(receivedMutex);
                    received.Append(StringView(reinterpret_cast<const utf8char*>(buffer),
                                               static_cast<usize>(n)));
                    sawHeaders = true;
                    // Two full events end with two blank-line terminators.
                    usize terminators = 0;
                    for (usize c = 0; c + 1 < received.Size(); ++c)
                    {
                        if (received[c] == u8'\n' && received[c + 1] == u8'\n')
                        {
                            ++terminators;
                        }
                    }
                    if (terminators >= 2)
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

    // Pump until the stream lands in the handler, write two events, then let the client read.
    for (u32 i = 0; i < 5000 && held.Get() == nullptr; ++i)
    {
        if (server.Pump() == 0)
        {
            SleepMilliseconds(1);
        }
    }
    REQUIRE(held.Get() != nullptr);
    CHECK(held->IsOpen());
    CHECK(held->WriteEvent(u8"progress", u8"cooking 1/2"));
    CHECK(held->WriteEvent(u8"", u8"line one\nline two"));
    for (u32 i = 0; i < 5000 && !clientDone; ++i)
    {
        (void)server.Pump();
        SleepMilliseconds(1);
    }
    client.Join();

    CHECK(sawHeaders);
    ScopedLock lock(receivedMutex);
    const StringView text = received.AsView();
    // Headers + both events arrived, with multi-line data split into per-line fields.
    CHECK(text.StartsWith(u8"HTTP/1.1 200 OK"));
    bool sawEventName = false;
    bool sawDataOne = false;
    bool sawDataTwo = false;
    for (usize i = 0; i < text.Size(); ++i)
    {
        if (text.SubStr(i, text.Size() - i).StartsWith(u8"event: progress"))
        {
            sawEventName = true;
        }
        if (text.SubStr(i, text.Size() - i).StartsWith(u8"data: line one"))
        {
            sawDataOne = true;
        }
        if (text.SubStr(i, text.Size() - i).StartsWith(u8"data: line two"))
        {
            sawDataTwo = true;
        }
    }
    CHECK(sawEventName);
    CHECK(sawDataOne);
    CHECK(sawDataTwo);

    // The consumer's ref outlives the peer: after the client is gone, writes turn false.
    held->Close();
    CHECK(!held->WriteEvent(u8"late", u8"nobody listening"));
}
