// Foundation::Mcp.Http - the `foundation.mcp.http` module.
//
// The MCP streamable-HTTP binding: the SAME McpServer registry the
// stdio host serves, over foundation.http - localhost + bearer token (the trust decision).
// A satellite of foundation.mcp (the Mcp.Reflection/Mcp.Script pattern), so the protocol
// core stays transport-free.
//
// Surface (the ez-validated subset, plus the SSE channel the user kept in scope):
//   POST /mcp     - one JSON-RPC message per request (the body is the line); the JSON-RPC
//                   response is the 200 body. A notification (no response) answers 202.
//   GET  /events  - the Server-Sent Events channel for server-initiated traffic (progress
//                   notifications, resource changes). Events are broadcast with
//                   McpHttpHost::Broadcast; a `: connected` comment confirms the stream.
//   Anything else - 404; wrong method on /mcp - 405; missing/wrong bearer token - 401
//                   (both endpoints; localhost is the boundary, the token is the lock).
//
// Threading matches the editor-host plan: Pump() from ONE thread (the editor's main thread,
// per frame) - handlers run synchronously there, so tools may touch main-thread state.
// Broadcast is safe from that same thread; SseStream itself serializes its writes.

module;
#include "Core/Prelude.h"

export module foundation.mcp.http;

import foundation.core;
import foundation.http;
import foundation.mcp;

using namespace foundation::core;

export namespace foundation::mcp
{
    struct McpHttpConfig
    {
        u16 port = 0;   // 0 = OS-assigned (read BoundPort after Start)
        String token;   // REQUIRED bearer token; Start refuses an empty one
    };

    /// Hosts an McpServer over HTTP + SSE. The McpServer must outlive the host.
    class McpHttpHost
    {
    public:
        explicit McpHttpHost(McpServer& server) : m_server(&server) {}
        ~McpHttpHost() { Stop(); }
        McpHttpHost(const McpHttpHost&) = delete;
        McpHttpHost& operator=(const McpHttpHost&) = delete;

        [[nodiscard]] bool Start(const McpHttpConfig& config);
        void Stop();
        [[nodiscard]] bool IsRunning() const noexcept { return m_http.IsRunning(); }
        [[nodiscard]] u16 BoundPort() const noexcept { return m_http.BoundPort(); }

        /// One pump of the underlying HTTP server (accept/read/dispatch/write). Call per
        /// frame or in a loop. Returns completed requests this call.
        usize Pump();

        /// Broadcast one event to every connected /events listener. Returns how many
        /// listeners received it (closed streams are dropped).
        usize Broadcast(StringView eventName, StringView data);
        [[nodiscard]] usize ListenerCount() const noexcept { return m_listeners.Size(); }

    private:
        [[nodiscard]] http::HttpResponse Handle(const http::HttpRequest& request);
        [[nodiscard]] bool Authorized(const http::HttpRequest& request) const;

        McpServer* m_server;
        http::HttpServer m_http;
        String m_token;
        Array<RefPtr<http::SseStream>> m_listeners;
    };
}
