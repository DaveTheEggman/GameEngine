// Foundation::Mcp - `foundation.mcp` (JSON-RPC 2.0 + MCP lifecycle + tool/resource registry).
//
// The protocol layer (layer 2, over foundation.json). Transport-abstract, stdio
// first; tools/resources REGISTER against a McpServer (the script-facade pattern). Implements the
// v1 subset (initialize/tools/resources/ping); hosts
// (Tools.Mcp headless, editor-embedded) sit on top and contribute tools.

export module foundation.mcp;

export import :schema;
export import :server;
export import :transport;
