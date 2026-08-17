// Foundation::Mcp - :server partition
//
// The JSON-RPC 2.0 dispatcher + MCP lifecycle + tool/resource registry. HandleLine takes ONE
// newline-delimited message and returns the response line (or nothing for a notification). It always
// survives bad input. Two error layers, never conflated: PROTOCOL failures are JSON-RPC error
// responses; TOOL failures are SUCCESSFUL responses whose result carries isError:true + the real
// error text (an agent must see the underlying cook/import message, not a protocol failure).
//
// v1 subset only: initialize / notifications/initialized / tools.list / tools.call /
// resources.list / resources.read / ping. Capabilities advertise exactly {tools, resources}.

module;
#include "Core/Prelude.h"

export module foundation.mcp:server;

import foundation.core;
import foundation.json;
import :schema;

using namespace foundation::core;
using foundation::json::JsonValue;
namespace json = foundation::json;

export namespace foundation::mcp
{
    // Pinned MCP protocol revision. See https://modelcontextprotocol.io/specification/2025-06-18
    inline constexpr StringView kProtocolVersion = u8"2025-06-18";

    // JSON-RPC error codes (the subset we emit).
    enum class RpcError : i32
    {
        ParseError = -32700,
        InvalidRequest = -32600,
        MethodNotFound = -32601,
        InvalidParams = -32602,
        InternalError = -32603,
    };

    // A tool turns validated args into a JSON result, or an error MESSAGE. The error channel is a
    // String (not a bare ErrorCode) precisely so agents receive the real underlying text.
    using ToolResult = Result<JsonValue, String>;
    using ToolHandler = Function<ToolResult(const JsonValue& args)>;

    struct Tool
    {
        String name;
        String description;
        JsonValue inputSchema;
        ToolHandler handler;
    };

    // A resource exposes read-only text by URI; the reader returns content or an error message.
    using ResourceReader = Function<Result<String, String>()>;
    struct Resource
    {
        String uri;
        String name;
        String mimeType;
        String description;
        ResourceReader reader;
    };
}

namespace foundation::mcp::detail
{
    inline JsonValue Envelope(const JsonValue& id)
    {
        JsonValue r = JsonValue::MakeObject();
        r.Set(u8"jsonrpc", JsonValue::MakeString(u8"2.0"));
        r.Set(u8"id", id); // echoed verbatim - string OR number type is preserved by JsonValue
        return r;
    }
    inline JsonValue MakeResult(const JsonValue& id, JsonValue result)
    {
        JsonValue r = Envelope(id);
        r.Set(u8"result", Move(result));
        return r;
    }
    inline JsonValue MakeError(const JsonValue& id, RpcError code, String message)
    {
        JsonValue err = JsonValue::MakeObject();
        err.Set(u8"code", JsonValue::MakeNumber(static_cast<f64>(static_cast<i32>(code))));
        err.Set(u8"message", JsonValue::MakeString(Move(message)));
        JsonValue r = Envelope(id);
        r.Set(u8"error", Move(err));
        return r;
    }
}

export namespace foundation::mcp
{
    class McpServer
    {
        Array<Tool> m_tools;
        Array<Resource> m_resources;
        String m_serverName = String(u8"draconic-mcp");
        String m_serverVersion = String(u8"0.1.0");

    public:
        [[nodiscard]] StringView ServerName() const noexcept { return m_serverName.AsView(); }
        [[nodiscard]] StringView ServerVersion() const noexcept
        {
            return m_serverVersion.AsView();
        }

        void SetServerInfo(String name, String version)
        {
            m_serverName = Move(name);
            m_serverVersion = Move(version);
        }
        void RegisterTool(String name, String description, JsonValue schema, ToolHandler handler)
        {
            m_tools.PushBack(Tool{Move(name), Move(description), Move(schema), Move(handler)});
        }
        void RegisterResource(String uri, String name, String mimeType, String description,
                              ResourceReader reader)
        {
            m_resources.PushBack(
                Resource{Move(uri), Move(name), Move(mimeType), Move(description), Move(reader)});
        }
        [[nodiscard]] usize ToolCount() const noexcept { return m_tools.Size(); }
        [[nodiscard]] usize ResourceCount() const noexcept { return m_resources.Size(); }

        // Handle ONE JSON-RPC message. Returns the response line to write, or an empty Optional for a
        // notification (no response). Never throws; malformed input yields a protocol error line.
        [[nodiscard]] Optional<String> HandleLine(StringView line)
        {
            json::ParseResult parsed = json::Parse(line);
            if (!parsed.ok)
            {
                return detail::MakeError(JsonValue::MakeNull(), RpcError::ParseError,
                                         String(u8"Parse error"))
                    .ToString();
            }
            const JsonValue& msg = parsed.value;
            // Top-level arrays are batch requests - not supported (later MCP revisions dropped them).
            if (msg.IsArray())
            {
                return detail::MakeError(JsonValue::MakeNull(), RpcError::InvalidRequest,
                                         String(u8"Batch requests are not supported"))
                    .ToString();
            }
            if (!msg.IsObject())
            {
                return detail::MakeError(JsonValue::MakeNull(), RpcError::InvalidRequest,
                                         String(u8"Invalid Request"))
                    .ToString();
            }

            const JsonValue methodVal = msg.Get(u8"method");
            const bool hasId = msg.Has(u8"id");
            if (!methodVal.IsString())
            {
                if (hasId)
                {
                    return detail::MakeError(msg.Get(u8"id"), RpcError::InvalidRequest,
                                             String(u8"Invalid Request: 'method' must be a string"))
                        .ToString();
                }
                return {}; // malformed notification - ignored, per JSON-RPC
            }
            // No id member => notification. Unknown notifications (incl. notifications/cancelled) and
            // notifications/initialized are all accepted SILENTLY: never a response.
            if (!hasId)
            {
                return {};
            }

            const JsonValue id = msg.Get(u8"id");
            const JsonValue params = msg.Get(u8"params");
            return Dispatch(methodVal.AsString().AsView(), params, id).ToString();
        }

    private:
        [[nodiscard]] const Tool* FindTool(StringView name) const
        {
            for (usize i = 0; i < m_tools.Size(); ++i)
            {
                if (m_tools[i].name.AsView() == name)
                {
                    return &m_tools[i];
                }
            }
            return nullptr;
        }
        [[nodiscard]] const Resource* FindResource(StringView uri) const
        {
            for (usize i = 0; i < m_resources.Size(); ++i)
            {
                if (m_resources[i].uri.AsView() == uri)
                {
                    return &m_resources[i];
                }
            }
            return nullptr;
        }

        [[nodiscard]] JsonValue Dispatch(StringView method, const JsonValue& params,
                                         const JsonValue& id)
        {
            if (method == StringView(u8"initialize"))
            {
                JsonValue result = JsonValue::MakeObject();
                // We support exactly our pinned revision; echo it (equals the client's when supported).
                result.Set(u8"protocolVersion", JsonValue::MakeString(kProtocolVersion));
                JsonValue caps = JsonValue::MakeObject();
                caps.Set(u8"tools", JsonValue::MakeObject());
                caps.Set(u8"resources", JsonValue::MakeObject());
                result.Set(u8"capabilities", Move(caps));
                JsonValue info = JsonValue::MakeObject();
                info.Set(u8"name", JsonValue::MakeString(m_serverName));
                info.Set(u8"version", JsonValue::MakeString(m_serverVersion));
                result.Set(u8"serverInfo", Move(info));
                return detail::MakeResult(id, Move(result));
            }
            if (method == StringView(u8"ping"))
            {
                return detail::MakeResult(id, JsonValue::MakeObject());
            }
            if (method == StringView(u8"tools/list"))
            {
                JsonValue tools = JsonValue::MakeArray();
                for (usize i = 0; i < m_tools.Size(); ++i)
                {
                    JsonValue jt = JsonValue::MakeObject();
                    jt.Set(u8"name", JsonValue::MakeString(m_tools[i].name));
                    jt.Set(u8"description", JsonValue::MakeString(m_tools[i].description));
                    jt.Set(u8"inputSchema", m_tools[i].inputSchema);
                    tools.Add(Move(jt));
                }
                JsonValue result = JsonValue::MakeObject();
                result.Set(u8"tools", Move(tools));
                return detail::MakeResult(id, Move(result));
            }
            if (method == StringView(u8"tools/call"))
            {
                const JsonValue nameVal = params.Get(u8"name");
                if (!nameVal.IsString())
                {
                    return detail::MakeError(id, RpcError::InvalidParams,
                                             String(u8"tools/call requires a string 'name'"));
                }
                const Tool* tool = FindTool(nameVal.AsString().AsView());
                if (tool == nullptr)
                {
                    return detail::MakeError(id, RpcError::InvalidParams,
                                             Format(u8"unknown tool '{}'", nameVal.AsString().AsView()));
                }
                JsonValue args = params.Get(u8"arguments");
                if (args.IsNull())
                {
                    args = JsonValue::MakeObject();
                }
                Optional<String> schemaError = ValidateArgs(args, tool->inputSchema);
                if (schemaError.HasValue())
                {
                    return detail::MakeError(id, RpcError::InvalidParams, Move(schemaError.Value()));
                }
                // Run the tool. BOTH outcomes are successful JSON-RPC responses; a tool failure is
                // reported as isError content, not a protocol error.
                ToolResult outcome = tool->handler(args);
                JsonValue item = JsonValue::MakeObject();
                item.Set(u8"type", JsonValue::MakeString(u8"text"));
                JsonValue result = JsonValue::MakeObject();
                if (outcome.HasValue())
                {
                    item.Set(u8"text", JsonValue::MakeString(outcome.Value().ToString()));
                    result.Set(u8"isError", JsonValue::MakeBool(false));
                }
                else
                {
                    item.Set(u8"text", JsonValue::MakeString(Move(outcome.Error())));
                    result.Set(u8"isError", JsonValue::MakeBool(true));
                }
                JsonValue content = JsonValue::MakeArray();
                content.Add(Move(item));
                result.Set(u8"content", Move(content));
                return detail::MakeResult(id, Move(result));
            }
            if (method == StringView(u8"resources/list"))
            {
                JsonValue arr = JsonValue::MakeArray();
                for (usize i = 0; i < m_resources.Size(); ++i)
                {
                    JsonValue jr = JsonValue::MakeObject();
                    jr.Set(u8"uri", JsonValue::MakeString(m_resources[i].uri));
                    jr.Set(u8"name", JsonValue::MakeString(m_resources[i].name));
                    jr.Set(u8"mimeType", JsonValue::MakeString(m_resources[i].mimeType));
                    jr.Set(u8"description", JsonValue::MakeString(m_resources[i].description));
                    arr.Add(Move(jr));
                }
                JsonValue result = JsonValue::MakeObject();
                result.Set(u8"resources", Move(arr));
                return detail::MakeResult(id, Move(result));
            }
            if (method == StringView(u8"resources/read"))
            {
                const JsonValue uriVal = params.Get(u8"uri");
                if (!uriVal.IsString())
                {
                    return detail::MakeError(id, RpcError::InvalidParams,
                                             String(u8"resources/read requires a string 'uri'"));
                }
                const Resource* res = FindResource(uriVal.AsString().AsView());
                if (res == nullptr)
                {
                    return detail::MakeError(id, RpcError::InvalidParams,
                                             Format(u8"unknown resource '{}'", uriVal.AsString().AsView()));
                }
                Result<String, String> content = res->reader();
                if (!content.HasValue())
                {
                    return detail::MakeError(id, RpcError::InternalError, Move(content.Error()));
                }
                JsonValue entry = JsonValue::MakeObject();
                entry.Set(u8"uri", JsonValue::MakeString(res->uri));
                entry.Set(u8"mimeType", JsonValue::MakeString(res->mimeType));
                entry.Set(u8"text", JsonValue::MakeString(Move(content.Value())));
                JsonValue contents = JsonValue::MakeArray();
                contents.Add(Move(entry));
                JsonValue result = JsonValue::MakeObject();
                result.Set(u8"contents", Move(contents));
                return detail::MakeResult(id, Move(result));
            }
            return detail::MakeError(id, RpcError::MethodNotFound,
                                     Format(u8"method not found: {}", method));
        }
    };

    /// host_info - the ops-hygiene tool EVERY host registers (mcp-agent-access.md P1 item 2,
    /// from ezEngine's app_info): pid (a hung host is killed by pid), the build stamp
    /// (stale-binary detection), server + protocol versions, and whatever host-specific state
    /// the host supplies (the stdio host reports its open project). `buildStamp` is the host
    /// executable's BuildStamp() text; `hostState` may be empty.
    inline void RegisterHostInfoTool(McpServer& server, String buildStamp,
                                     Function<JsonValue()> hostState = {})
    {
        McpServer* s = &server;
        server.RegisterTool(
            u8"host_info",
            u8"The host process's identity: pid (kill a hung host by pid), buildStamp (detect a "
            u8"stale binary after a rebuild), server + MCP protocol versions, and host state "
            u8"(e.g. the open project). Read this first in a new session.",
            SchemaBuilder().Build(),
            [s, buildStamp = Move(buildStamp),
             hostState = Move(hostState)](const JsonValue&) -> ToolResult
            {
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"pid",
                        JsonValue::MakeNumber(static_cast<f64>(foundation::core::ProcessId())));
                out.Set(u8"buildStamp", JsonValue::MakeString(buildStamp));
                out.Set(u8"serverName", JsonValue::MakeString(String(s->ServerName())));
                out.Set(u8"serverVersion", JsonValue::MakeString(String(s->ServerVersion())));
                out.Set(u8"protocolVersion", JsonValue::MakeString(String(kProtocolVersion)));
                if (hostState)
                {
                    out.Set(u8"host", hostState());
                }
                return out;
            });
    }
}
