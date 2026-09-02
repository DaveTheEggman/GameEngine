// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Foundation::Http - the `foundation.http` module.
//
// A deliberately SMALL HTTP/1.1 layer over the Core/System TCP sockets (via foundation.net's
// TcpSocket/TcpListener): the substrate for the MCP streamable-HTTP host, the WebSocket
// upgrade path (web networking - a WS connection BEGINS as an HTTP request), and loopback
// tooling/tests. Not a general web stack, by design:
//   - HTTP/1.1 only; ONE request per connection (Connection: close) - the ez-validated MCP
//     transport subset. No pipelining, no keep-alive, no chunked request bodies, no TLS
//     (localhost trust domain; auth is a bearer token at the consumer layer).
//   - The ONE deliberate exception to one-shot: a handler may answer with an EVENT STREAM
//     (Server-Sent Events) - the connection then stays open and the consumer writes events
//     through a ref-counted SseStream for as long as the peer listens.
//   - The client is BLOCKING with a timeout, localhost-focused (dotted-quad address, no
//     DNS): tests, local tooling, and the MCP acceptance loop - not a game-facing fetch API.
//
// Threading: HttpServer is single-threaded - Start/Pump/Stop from ONE thread (pump it from a
// dedicated thread or a frame loop). SseStream::WriteEvent is safe from any ONE writer thread
// at a time (the stream serializes writes internally against Close).

module;
#include "Core/Prelude.h"

export module foundation.http;

import foundation.core;
import foundation.net;

using namespace foundation::core;

export namespace foundation::http
{
    struct HttpHeader
    {
        String name;
        String value;
    };

    // Case-insensitive (ASCII) header lookup; empty view when absent.
    [[nodiscard]] StringView FindHeader(const Array<HttpHeader>& headers, StringView name);

    struct HttpRequest
    {
        String method; // "GET", "POST", ... (verbatim, upper-case per spec)
        String target; // origin-form target, verbatim ("/mcp", "/events?since=5")
        Array<HttpHeader> headers;
        Array<byte> body;

        [[nodiscard]] StringView Header(StringView name) const
        {
            return FindHeader(headers, name);
        }
        [[nodiscard]] StringView BodyText() const
        {
            return StringView(reinterpret_cast<const utf8char*>(body.Data()), body.Size());
        }
    };

    struct HttpResponse
    {
        i32 status = 200;
        Array<HttpHeader> headers; // Content-Length + Connection are written by the server
        Array<byte> body;
        bool eventStream = false; // true = SSE: headers go out, the connection stays open

        [[nodiscard]] StringView Header(StringView name) const
        {
            return FindHeader(headers, name);
        }
        [[nodiscard]] StringView BodyText() const
        {
            return StringView(reinterpret_cast<const utf8char*>(body.Data()), body.Size());
        }

        [[nodiscard]] static HttpResponse Text(i32 status, StringView contentType,
                                               StringView text);
        [[nodiscard]] static HttpResponse Json(i32 status, StringView jsonText);
        /// The SSE marker response: the server writes `text/event-stream` headers and hands
        /// the held connection to the stream handler instead of closing.
        [[nodiscard]] static HttpResponse EventStream();
    };

    [[nodiscard]] StringView HttpStatusText(i32 status) noexcept;

    // ---- incremental message parser ------------------------------------------------------

    enum class HttpParseState : u8
    {
        NeedMore, // feed more bytes
        Complete, // a full message is available
        Failed,   // malformed / over limits - close the connection (server answers 400)
    };

    /// Incremental HTTP/1.1 message parser (request or response mode). Push bytes as they
    /// arrive; when Complete, the parsed pieces are readable. Strict subset: CRLF framing,
    /// Content-Length bodies only (chunked -> Failed), 16 KiB header cap, caller body cap.
    /// Response mode without Content-Length = read-until-close (caller calls OnPeerClosed).
    class HttpMessageParser
    {
    public:
        enum class Mode : u8
        {
            Request,
            Response
        };

        explicit HttpMessageParser(Mode mode, usize maxBodyBytes = 16 * 1024 * 1024)
            : m_mode(mode), m_maxBody(maxBodyBytes)
        {
        }

        [[nodiscard]] HttpParseState Push(Span<const byte> bytes);
        /// Response mode: the peer closed - a close-delimited body is now complete.
        [[nodiscard]] HttpParseState OnPeerClosed();
        [[nodiscard]] HttpParseState State() const noexcept { return m_state; }

        // Request mode results:
        [[nodiscard]] const String& Method() const noexcept { return m_method; }
        [[nodiscard]] const String& Target() const noexcept { return m_target; }
        // Response mode result:
        [[nodiscard]] i32 Status() const noexcept { return m_status; }
        // Both:
        [[nodiscard]] const Array<HttpHeader>& Headers() const noexcept { return m_headers; }
        [[nodiscard]] Array<byte>& Body() noexcept { return m_body; }

    private:
        [[nodiscard]] bool ParseHead();

        Mode m_mode;
        usize m_maxBody;
        HttpParseState m_state = HttpParseState::NeedMore;
        bool m_headParsed = false;
        bool m_bodyUntilClose = false;
        usize m_bodyExpected = 0;
        Array<byte> m_buffer; // head accumulation (capped), then body bytes append to m_body
        String m_method;
        String m_target;
        i32 m_status = 0;
        Array<HttpHeader> m_headers;
        Array<byte> m_body;
    };

    // ---- server --------------------------------------------------------------------------

    /// A held Server-Sent Events connection. Ref-counted: the server keeps one ref (dropped
    /// when the peer disconnects or the server stops), the consumer keeps another and writes
    /// events until IsOpen() turns false. Writes after close are safe no-ops returning false.
    class SseStream final : public RefCounted
    {
    public:
        explicit SseStream(foundation::net::TcpSocket socket) : m_socket(Move(socket)) {}

        /// Send one event: `event: <name>` (omitted when empty) + per-line `data:` fields +
        /// the blank-line terminator. False when the peer is gone (the stream closes itself).
        bool WriteEvent(StringView eventName, StringView data);
        /// A comment line (": <text>") - the SSE keep-alive idiom.
        bool WriteComment(StringView text);
        [[nodiscard]] bool IsOpen() const;
        /// Liveness probe: reads (and discards) any client chatter; a closed peer is detected
        /// promptly (a WRITE into a freshly closed socket can still succeed - TCP buffers it
        /// until the reset arrives - so sweeps poll this instead). False = peer gone (the
        /// stream closes itself).
        [[nodiscard]] bool PollLive();
        void Close();

    private:
        bool SendAll(Span<const byte> bytes);

        mutable Mutex m_mutex;
        foundation::net::TcpSocket m_socket;
    };

    struct HttpServerConfig
    {
        u16 port = 0; // 0 = OS-assigned (read BoundPort after Start)
        usize maxBodyBytes = 16 * 1024 * 1024;
        usize maxConnections = 32; // accepted-but-unanswered connections beyond this are refused
    };

    /// The pump-model HTTP server: Start binds the listener; each Pump() accepts pending
    /// connections, reads, and for every COMPLETE request calls the handler and writes the
    /// response (Connection: close) - or, for an EventStream() response, writes the SSE
    /// headers and hands the connection to the stream handler. Malformed input answers 400
    /// and closes; a missing handler answers 404. Single-threaded (see module header).
    class HttpServer
    {
    public:
        // The allocator (required - the owner decides) backs the listener,
        // connections, and SSE streams.
        explicit HttpServer(IAllocator& allocator) noexcept : m_allocator(&allocator) {}
        ~HttpServer() { Stop(); }
        HttpServer(const HttpServer&) = delete;
        HttpServer& operator=(const HttpServer&) = delete;

        [[nodiscard]] bool Start(const HttpServerConfig& config);
        void Stop();
        [[nodiscard]] bool IsRunning() const noexcept { return m_listener.Get() != nullptr; }
        [[nodiscard]] u16 BoundPort() const noexcept;

        /// The request handler (one-shot responses AND the EventStream() marker).
        void SetHandler(Function<HttpResponse(const HttpRequest&)> handler)
        {
            m_handler = Move(handler);
        }
        /// Called when a handler answered EventStream(): the consumer takes its ref and
        /// writes events for as long as it likes.
        void SetStreamHandler(Function<void(const HttpRequest&, RefPtr<SseStream>)> handler)
        {
            m_streamHandler = Move(handler);
        }

        /// One pump: accept + read + dispatch + write. Returns the number of requests
        /// completed this call (0 = nothing happened; callers may sleep briefly on 0).
        usize Pump();

    private:
        struct Connection
        {
            foundation::net::TcpSocket socket;
            HttpMessageParser parser{HttpMessageParser::Mode::Request};
            Connection(foundation::net::TcpSocket s, usize maxBody)
                : socket(Move(s)), parser(HttpMessageParser::Mode::Request, maxBody)
            {
            }
        };

        void Dispatch(Connection& connection);
        static void WriteResponse(foundation::net::TcpSocket& socket, const HttpResponse& r);

        HttpServerConfig m_config;
        IAllocator* m_allocator;
        UniquePtr<foundation::net::TcpListener> m_listener;
        Array<UniquePtr<Connection>> m_connections;
        Array<RefPtr<SseStream>> m_streams; // server-side refs; swept when closed
        Function<HttpResponse(const HttpRequest&)> m_handler;
        Function<void(const HttpRequest&, RefPtr<SseStream>)> m_streamHandler;
    };

    // ---- client --------------------------------------------------------------------------

    /// BLOCKING one-shot HTTP request against a dotted-quad address (no DNS - localhost
    /// tooling). Connects, sends, reads the full response (Content-Length or until close),
    /// enforcing `timeoutMilliseconds` across the whole exchange. Error = a human-readable
    /// reason (connect failed / timeout / malformed response).
    [[nodiscard]] Result<HttpResponse, String>
    HttpFetch(StringView address, u16 port, const HttpRequest& request,
              u32 timeoutMilliseconds = 5000);
}
