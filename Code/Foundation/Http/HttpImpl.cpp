// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Foundation::Http - implementation unit (parser, server pump, SSE writes, blocking client).

module;
#include "Core/Prelude.h"
#include <cstring>

module foundation.http;

import foundation.core;
import foundation.net;

using namespace foundation::core;
namespace net = foundation::net;

namespace foundation::http
{
    namespace
    {
        inline constexpr usize kMaxHeadBytes = 16 * 1024;

        [[nodiscard]] bool AsciiEqualsIgnoreCase(StringView a, StringView b)
        {
            if (a.Size() != b.Size())
            {
                return false;
            }
            for (usize i = 0; i < a.Size(); ++i)
            {
                utf8char ca = a[i];
                utf8char cb = b[i];
                if (ca >= u8'A' && ca <= u8'Z')
                {
                    ca = static_cast<utf8char>(ca + (u8'a' - u8'A'));
                }
                if (cb >= u8'A' && cb <= u8'Z')
                {
                    cb = static_cast<utf8char>(cb + (u8'a' - u8'A'));
                }
                if (ca != cb)
                {
                    return false;
                }
            }
            return true;
        }

        void AppendBytes(Array<byte>& out, const byte* data, usize size)
        {
            if (size == 0)
            {
                return;
            }
            const usize oldSize = out.Size();
            out.Resize(oldSize + size);
            std::memcpy(out.Data() + oldSize, data, size);
        }

        void AppendText(Array<byte>& out, StringView text)
        {
            AppendBytes(out, reinterpret_cast<const byte*>(text.Data()), text.Size());
        }

        // Digits-only parse; false on empty/garbage/overflowing values.
        [[nodiscard]] bool ParseUnsigned(StringView text, usize& out)
        {
            if (text.IsEmpty())
            {
                return false;
            }
            usize value = 0;
            for (usize i = 0; i < text.Size(); ++i)
            {
                const utf8char c = text[i];
                if (c < u8'0' || c > u8'9')
                {
                    return false;
                }
                if (value > (~usize(0) - 9) / 10)
                {
                    return false;
                }
                value = value * 10 + static_cast<usize>(c - u8'0');
            }
            out = value;
            return true;
        }
    }

    StringView FindHeader(const Array<HttpHeader>& headers, StringView name)
    {
        for (const HttpHeader& h : headers)
        {
            if (AsciiEqualsIgnoreCase(h.name.AsView(), name))
            {
                return h.value.AsView();
            }
        }
        return {};
    }

    HttpResponse HttpResponse::Text(i32 status, StringView contentType, StringView text)
    {
        HttpResponse r;
        r.status = status;
        r.headers.PushBack(HttpHeader{String(u8"Content-Type"), String(contentType)});
        AppendText(r.body, text);
        return r;
    }

    HttpResponse HttpResponse::Json(i32 status, StringView jsonText)
    {
        return Text(status, u8"application/json", jsonText);
    }

    HttpResponse HttpResponse::EventStream()
    {
        HttpResponse r;
        r.status = 200;
        r.eventStream = true;
        return r;
    }

    StringView HttpStatusText(i32 status) noexcept
    {
        switch (status)
        {
        case 200: return u8"OK";
        case 202: return u8"Accepted";
        case 204: return u8"No Content";
        case 400: return u8"Bad Request";
        case 401: return u8"Unauthorized";
        case 404: return u8"Not Found";
        case 405: return u8"Method Not Allowed";
        case 413: return u8"Content Too Large";
        case 500: return u8"Internal Server Error";
        case 501: return u8"Not Implemented";
        case 503: return u8"Service Unavailable";
        default: return u8"Status";
        }
    }

    // ---- parser --------------------------------------------------------------------------

    bool HttpMessageParser::ParseHead()
    {
        const StringView head(reinterpret_cast<const utf8char*>(m_buffer.Data()),
                              m_buffer.Size());
        usize lineStart = 0;
        bool firstLine = true;
        while (lineStart < head.Size())
        {
            usize lineEnd = lineStart;
            while (lineEnd + 1 < head.Size() &&
                   !(head[lineEnd] == u8'\r' && head[lineEnd + 1] == u8'\n'))
            {
                ++lineEnd;
            }
            const StringView line = head.SubStr(lineStart, lineEnd - lineStart);
            lineStart = lineEnd + 2;
            if (line.IsEmpty())
            {
                break;
            }
            if (firstLine)
            {
                firstLine = false;
                // Request:  METHOD SP TARGET SP HTTP/1.x
                // Response: HTTP/1.x SP STATUS SP REASON
                usize firstSpace = 0;
                while (firstSpace < line.Size() && line[firstSpace] != u8' ')
                {
                    ++firstSpace;
                }
                usize secondSpace = firstSpace + 1;
                while (secondSpace < line.Size() && line[secondSpace] != u8' ')
                {
                    ++secondSpace;
                }
                if (firstSpace >= line.Size() || secondSpace > line.Size())
                {
                    return false;
                }
                if (m_mode == Mode::Request)
                {
                    const StringView version =
                        line.SubStr(secondSpace + 1, line.Size() - secondSpace - 1);
                    if (!version.StartsWith(u8"HTTP/1."))
                    {
                        return false;
                    }
                    m_method = String(line.SubStr(0, firstSpace));
                    m_target =
                        String(line.SubStr(firstSpace + 1, secondSpace - firstSpace - 1));
                    if (m_method.IsEmpty() || m_target.IsEmpty())
                    {
                        return false;
                    }
                }
                else
                {
                    if (!line.StartsWith(u8"HTTP/1."))
                    {
                        return false;
                    }
                    usize statusValue = 0;
                    if (!ParseUnsigned(
                            line.SubStr(firstSpace + 1, secondSpace - firstSpace - 1),
                            statusValue) ||
                        statusValue > 999)
                    {
                        return false;
                    }
                    m_status = static_cast<i32>(statusValue);
                }
                continue;
            }
            // Header line: name ":" OWS value.
            usize colon = 0;
            while (colon < line.Size() && line[colon] != u8':')
            {
                ++colon;
            }
            if (colon == 0 || colon >= line.Size())
            {
                return false;
            }
            StringView value = line.SubStr(colon + 1, line.Size() - colon - 1);
            value = Trim(value);
            m_headers.PushBack(HttpHeader{String(line.SubStr(0, colon)), String(value)});
        }

        // Body framing: Content-Length only. Chunked (either direction) is out of subset.
        if (!FindHeader(m_headers, u8"Transfer-Encoding").IsEmpty())
        {
            return false;
        }
        const StringView contentLength = FindHeader(m_headers, u8"Content-Length");
        if (!contentLength.IsEmpty())
        {
            if (!ParseUnsigned(contentLength, m_bodyExpected) || m_bodyExpected > m_maxBody)
            {
                return false;
            }
        }
        else if (m_mode == Mode::Response)
        {
            m_bodyUntilClose = true; // close-delimited response body
        }
        return true;
    }

    HttpParseState HttpMessageParser::Push(Span<const byte> bytes)
    {
        if (m_state != HttpParseState::NeedMore)
        {
            return m_state;
        }
        usize offset = 0;
        if (!m_headParsed)
        {
            AppendBytes(m_buffer, bytes.Data(), bytes.Size());
            if (m_buffer.Size() > kMaxHeadBytes)
            {
                m_state = HttpParseState::Failed;
                return m_state;
            }
            // Find the CRLFCRLF head terminator.
            usize headEnd = 0;
            bool found = false;
            for (usize i = 0; i + 3 < m_buffer.Size(); ++i)
            {
                if (m_buffer[i] == byte{'\r'} && m_buffer[i + 1] == byte{'\n'} &&
                    m_buffer[i + 2] == byte{'\r'} && m_buffer[i + 3] == byte{'\n'})
                {
                    headEnd = i + 4;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return m_state; // NeedMore
            }
            const usize bodyStart = headEnd;
            const usize extra = m_buffer.Size() - bodyStart;
            Array<byte> spill;
            AppendBytes(spill, m_buffer.Data() + bodyStart, extra);
            m_buffer.Resize(headEnd);
            if (!ParseHead())
            {
                m_state = HttpParseState::Failed;
                return m_state;
            }
            m_headParsed = true;
            m_buffer.Clear();
            AppendBytes(m_body, spill.Data(), spill.Size());
            offset = bytes.Size(); // everything already consumed via m_buffer
        }
        else
        {
            AppendBytes(m_body, bytes.Data(), bytes.Size());
            offset = bytes.Size();
        }
        (void)offset;

        if (m_bodyUntilClose)
        {
            if (m_body.Size() > m_maxBody)
            {
                m_state = HttpParseState::Failed;
            }
            return m_state; // NeedMore until the peer closes
        }
        if (m_body.Size() > m_maxBody)
        {
            m_state = HttpParseState::Failed;
            return m_state;
        }
        if (m_body.Size() >= m_bodyExpected)
        {
            m_body.Resize(m_bodyExpected); // drop any over-read (Connection: close model)
            m_state = HttpParseState::Complete;
        }
        return m_state;
    }

    HttpParseState HttpMessageParser::OnPeerClosed()
    {
        if (m_state != HttpParseState::NeedMore)
        {
            return m_state;
        }
        if (m_headParsed && m_bodyUntilClose)
        {
            m_state = HttpParseState::Complete;
        }
        else
        {
            m_state = HttpParseState::Failed; // closed mid-message
        }
        return m_state;
    }

    // ---- SSE stream ----------------------------------------------------------------------

    bool SseStream::SendAll(Span<const byte> bytes)
    {
        usize sent = 0;
        u32 patience = 2000; // ~2 s of 1 ms would-block waits (localhost: effectively never)
        while (sent < bytes.Size())
        {
            const i64 n = m_socket.Send(
                Span<const byte>(bytes.Data() + sent, bytes.Size() - sent));
            if (n < 0)
            {
                m_socket.Close();
                return false;
            }
            if (n == 0)
            {
                if (patience-- == 0)
                {
                    m_socket.Close();
                    return false;
                }
                SleepMilliseconds(1);
                continue;
            }
            sent += static_cast<usize>(n);
        }
        return true;
    }

    bool SseStream::WriteEvent(StringView eventName, StringView data)
    {
        ScopedLock lock(m_mutex);
        if (!m_socket.IsOpen())
        {
            return false;
        }
        Array<byte> out;
        if (!eventName.IsEmpty())
        {
            AppendText(out, u8"event: ");
            AppendText(out, eventName);
            AppendText(out, u8"\n");
        }
        // Multi-line data -> one `data:` field per line (the SSE framing rule).
        usize lineStart = 0;
        for (usize i = 0; i <= data.Size(); ++i)
        {
            if (i == data.Size() || data[i] == u8'\n')
            {
                AppendText(out, u8"data: ");
                AppendText(out, data.SubStr(lineStart, i - lineStart));
                AppendText(out, u8"\n");
                lineStart = i + 1;
            }
        }
        AppendText(out, u8"\n");
        return SendAll(Span<const byte>(out.Data(), out.Size()));
    }

    bool SseStream::WriteComment(StringView text)
    {
        ScopedLock lock(m_mutex);
        if (!m_socket.IsOpen())
        {
            return false;
        }
        Array<byte> out;
        AppendText(out, u8": ");
        AppendText(out, text);
        AppendText(out, u8"\n\n");
        return SendAll(Span<const byte>(out.Data(), out.Size()));
    }

    bool SseStream::IsOpen() const
    {
        ScopedLock lock(m_mutex);
        return m_socket.IsOpen();
    }

    bool SseStream::PollLive()
    {
        ScopedLock lock(m_mutex);
        if (!m_socket.IsOpen())
        {
            return false;
        }
        byte buffer[256];
        for (;;)
        {
            const i64 n = m_socket.Receive(Span<byte>(buffer, sizeof(buffer)));
            if (n == 0)
            {
                return true; // would-block: the peer is quiet but alive
            }
            if (n < 0)
            {
                m_socket.Close(); // closed or errored - the stream is dead
                return false;
            }
            // n > 0: an SSE client should not send; discard and keep probing.
        }
    }

    void SseStream::Close()
    {
        ScopedLock lock(m_mutex);
        m_socket.Close();
    }

    // ---- server --------------------------------------------------------------------------

    bool HttpServer::Start(const HttpServerConfig& config)
    {
        Stop();
        m_config = config;
        m_listener = MakeUnique<net::TcpListener>(*m_allocator, config.port);
        if (!m_listener->IsOpen())
        {
            m_listener = nullptr;
            return false;
        }
        return true;
    }

    void HttpServer::Stop()
    {
        m_connections.Clear();
        for (RefPtr<SseStream>& stream : m_streams)
        {
            stream->Close();
        }
        m_streams.Clear();
        m_listener = nullptr;
    }

    u16 HttpServer::BoundPort() const noexcept
    {
        return m_listener.Get() != nullptr ? m_listener->BoundPort() : 0;
    }

    void HttpServer::WriteResponse(net::TcpSocket& socket, const HttpResponse& r)
    {
        StringBuilder head;
        head.Append(u8"HTTP/1.1 ");
        head.Append(Format(u8"{}", r.status).AsView());
        head.Append(u8' ');
        head.Append(HttpStatusText(r.status));
        head.Append(u8"\r\n");
        for (const HttpHeader& h : r.headers)
        {
            head.Append(h.name.AsView());
            head.Append(u8": ");
            head.Append(h.value.AsView());
            head.Append(u8"\r\n");
        }
        head.Append(Format(u8"Content-Length: {}\r\n", r.body.Size()).AsView());
        head.Append(u8"Connection: close\r\n\r\n");
        const String headText = head.Take();

        Array<byte> wire;
        AppendText(wire, headText.AsView());
        AppendBytes(wire, r.body.Data(), r.body.Size());
        usize sent = 0;
        u32 patience = 2000;
        while (sent < wire.Size())
        {
            const i64 n =
                socket.Send(Span<const byte>(wire.Data() + sent, wire.Size() - sent));
            if (n < 0)
            {
                return;
            }
            if (n == 0)
            {
                if (patience-- == 0)
                {
                    return;
                }
                SleepMilliseconds(1);
                continue;
            }
            sent += static_cast<usize>(n);
        }
    }

    void HttpServer::Dispatch(Connection& connection)
    {
        HttpRequest request;
        request.method = connection.parser.Method();
        request.target = connection.parser.Target();
        request.headers = connection.parser.Headers();
        request.body = Move(connection.parser.Body());

        HttpResponse response =
            m_handler ? m_handler(request)
                      : HttpResponse::Text(404, u8"text/plain", u8"no handler registered");
        if (response.eventStream)
        {
            // SSE: write the stream headers, then hand the held connection over.
            StringBuilder head;
            head.Append(u8"HTTP/1.1 200 OK\r\n"
                        u8"Content-Type: text/event-stream\r\n"
                        u8"Cache-Control: no-store\r\n"
                        u8"Connection: close\r\n\r\n");
            const String headText = head.Take();
            const i64 sent = connection.socket.Send(Span<const byte>(
                reinterpret_cast<const byte*>(headText.Data()), headText.Size()));
            if (sent == static_cast<i64>(headText.Size()))
            {
                RefPtr<SseStream> stream =
                    MakeRef<SseStream>(*m_allocator, Move(connection.socket));
                m_streams.PushBack(stream);
                if (m_streamHandler)
                {
                    m_streamHandler(request, stream);
                }
            }
            return;
        }
        WriteResponse(connection.socket, response);
    }

    usize HttpServer::Pump()
    {
        if (m_listener.Get() == nullptr)
        {
            return 0;
        }
        // Accept everything pending (refusing beyond the connection cap by dropping).
        for (;;)
        {
            net::TcpSocket accepted = m_listener->Accept();
            if (!accepted.IsOpen())
            {
                break;
            }
            if (m_connections.Size() >= m_config.maxConnections)
            {
                continue; // socket closes on scope exit = refused
            }
            m_connections.PushBack(MakeUnique<Connection>(*m_allocator, Move(accepted),
                                                          m_config.maxBodyBytes));
        }

        usize completed = 0;
        for (usize i = 0; i < m_connections.Size();)
        {
            Connection& connection = *m_connections[i];
            bool done = false;
            byte buffer[4096];
            for (;;)
            {
                const i64 n = connection.socket.Receive(Span<byte>(buffer, sizeof(buffer)));
                if (n > 0)
                {
                    const HttpParseState state = connection.parser.Push(
                        Span<const byte>(buffer, static_cast<usize>(n)));
                    if (state == HttpParseState::Complete)
                    {
                        Dispatch(connection);
                        ++completed;
                        done = true;
                        break;
                    }
                    if (state == HttpParseState::Failed)
                    {
                        WriteResponse(connection.socket,
                                      HttpResponse::Text(400, u8"text/plain",
                                                         u8"malformed HTTP request"));
                        done = true;
                        break;
                    }
                    continue;
                }
                if (n == 0)
                {
                    break; // would-block: keep the connection, try next pump
                }
                done = true; // peer closed before a complete request
                break;
            }
            if (done)
            {
                m_connections.RemoveAt(i);
            }
            else
            {
                ++i;
            }
        }

        // Sweep dead event streams (liveness-POLLED: a write into a freshly closed peer can
        // still succeed, so IsOpen alone lags). The consumer's ref stays valid - writes no-op.
        for (usize i = 0; i < m_streams.Size();)
        {
            if (!m_streams[i]->PollLive())
            {
                m_streams.RemoveAt(i);
            }
            else
            {
                ++i;
            }
        }
        return completed;
    }

    // ---- client --------------------------------------------------------------------------

    Result<HttpResponse, String> HttpFetch(StringView address, u16 port,
                                           const HttpRequest& request,
                                           u32 timeoutMilliseconds)
    {
        net::TcpSocket socket = net::TcpSocket::Connect(address, port);
        if (!socket.IsOpen())
        {
            return Err(Format(u8"could not open a socket to {}:{}", address, port));
        }
        u32 budget = timeoutMilliseconds;
        for (;;)
        {
            const int status = socket.ConnectStatus();
            if (status > 0)
            {
                break;
            }
            if (status < 0)
            {
                return Err(Format(u8"connect to {}:{} failed", address, port));
            }
            if (budget-- == 0)
            {
                return Err(Format(u8"connect to {}:{} timed out", address, port));
            }
            SleepMilliseconds(1);
        }

        // Request wire form.
        StringBuilder head;
        head.Append(request.method.IsEmpty() ? StringView(u8"GET") : request.method.AsView());
        head.Append(u8' ');
        head.Append(request.target.IsEmpty() ? StringView(u8"/") : request.target.AsView());
        head.Append(u8" HTTP/1.1\r\nHost: ");
        head.Append(address);
        head.Append(u8"\r\n");
        for (const HttpHeader& h : request.headers)
        {
            head.Append(h.name.AsView());
            head.Append(u8": ");
            head.Append(h.value.AsView());
            head.Append(u8"\r\n");
        }
        head.Append(Format(u8"Content-Length: {}\r\n", request.body.Size()).AsView());
        head.Append(u8"Connection: close\r\n\r\n");
        const String headText = head.Take();

        Array<byte> wire;
        AppendText(wire, headText.AsView());
        AppendBytes(wire, request.body.Data(), request.body.Size());
        usize sent = 0;
        while (sent < wire.Size())
        {
            const i64 n =
                socket.Send(Span<const byte>(wire.Data() + sent, wire.Size() - sent));
            if (n < 0)
            {
                return Err(String(u8"connection closed while sending the request"));
            }
            if (n == 0)
            {
                if (budget-- == 0)
                {
                    return Err(String(u8"request send timed out"));
                }
                SleepMilliseconds(1);
                continue;
            }
            sent += static_cast<usize>(n);
        }

        // Read the response until Complete (Content-Length) or the peer closes.
        HttpMessageParser parser(HttpMessageParser::Mode::Response);
        byte buffer[4096];
        for (;;)
        {
            const i64 n = socket.Receive(Span<byte>(buffer, sizeof(buffer)));
            if (n > 0)
            {
                const HttpParseState state =
                    parser.Push(Span<const byte>(buffer, static_cast<usize>(n)));
                if (state == HttpParseState::Complete)
                {
                    break;
                }
                if (state == HttpParseState::Failed)
                {
                    return Err(String(u8"malformed HTTP response"));
                }
                continue;
            }
            if (n == 0)
            {
                if (budget-- == 0)
                {
                    return Err(String(u8"response read timed out"));
                }
                SleepMilliseconds(1);
                continue;
            }
            if (parser.OnPeerClosed() == HttpParseState::Complete)
            {
                break;
            }
            return Err(String(u8"connection closed mid-response"));
        }

        HttpResponse response;
        response.status = parser.Status();
        response.headers = parser.Headers();
        response.body = Move(parser.Body());
        return response;
    }
}
