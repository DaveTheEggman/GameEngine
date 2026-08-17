// Tools.Mcp - headless MCP stdio server (the P1 host).
//
// Speaks newline-delimited JSON-RPC over stdin/stdout so an agent (e.g. Claude Code, one command
// line) gets the engine's introspection surface with the editor CLOSED. STDOUT IS THE WIRE - every
// engine log is routed to stderr instead (a stray stdout print corrupts the protocol stream).
//
// v1 surface: the reflection tools (type_list / type_info) over the Core + JSON reflected types.
// The tool surface grows as modules contribute their own (project/pipeline/scene tools next).

#include "Core/Prelude.h"

#include <cstdio>

import foundation.core;
import foundation.json;
import foundation.mcp;
import foundation.mcp.reflection;
import foundation.mcp.script;
import pipeline.core;
import pipeline.importer;
import pipeline.registration;
import engine.scriptsurface;
#ifdef OPTION_HAS_LUAU
import foundation.script.luau;
#endif
import editor.mcp;

using namespace foundation::core;
using namespace foundation::mcp;

extern "C" const char* BuildStamp();

namespace
{
    // Routes ALL engine log lines to stderr, keeping stdout clean for the JSON-RPC wire.
    class StderrSink final : public ILogSink
    {
    public:
        void Write(LogLevel level, StringView category, StringView message) noexcept override
        {
            StringBuilder line;
            line.Append(u8'[');
            line.Append(LogLevelName(level));
            line.Append(u8"] ");
            line.Append(category);
            line.Append(u8": ");
            line.Append(message);
            line.Append(u8'\n');
            const String text = line.Take();
            std::fwrite(text.Data(), 1, text.Size(), stderr);
            std::fflush(stderr);
        }
    };
}

int main(int /*argc*/, char** /*argv*/)
{
    static StderrSink stderrSink;
    GlobalLogger().AddSink(&stderrSink); // logs -> stderr; stdout is the protocol stream

    // Populate the reflection registry with the surface we can introspect headlessly, plus the
    // full pipeline type set (so type_list sees every asset/product type and asset_cook can build).
    RegisterCoreTypes();
    foundation::json::RegisterJsonTypes();
    pipeline::RegisterPipelineTypes();
    // The COMPLETE engine script surface (every subsystem facade), so script_api reports the whole
    // bound API, not just core - metadata only, no subsystem instantiated (headless). Plus Luau, so
    // script_api spans wren | angelscript | luau (the pipeline types already registered wren + as).
    engine::RegisterAllScriptFacades();
#ifdef OPTION_HAS_LUAU
    foundation::script::RegisterLuauScriptBackend(); // wren + angelscript come from pipeline.registration
#endif

    // The host's builder + importer registries (from the pipeline composition root); populated
    // once, they outlive the server and back asset_cook / asset_import.
    pipeline::BuilderRegistry builders;
    pipeline::ImporterRegistry importers;
    pipeline::RegisterAllBuilders(builders);
    pipeline::RegisterAllImporters(importers);

    McpServer server;
    server.SetServerInfo(u8"draconic-mcp", u8"0.1.0");
    RegisterReflectionTools(server);
    RegisterScriptTools(server); // script_api: the per-backend bound API for writing scripts

    // The host's current project (project_open/create populate it); outlives the server.
    editor::mcp::ProjectSession session;
    editor::mcp::RegisterProjectTools(server, session);
    editor::mcp::RegisterAssetTools(server, session);
    editor::mcp::RegisterAssetWriteTools(server, session, builders, importers);
    editor::mcp::RegisterSceneTools(server, session); // scene/prefab read+write+validate (files-first)
    // host_info (ops hygiene): pid + build stamp + versions + the open-project state.
    RegisterHostInfoTool(
        server, String(reinterpret_cast<const char8_t*>(BuildStamp())),
        Function<foundation::json::JsonValue()>{
            [&session]()
            {
                using foundation::json::JsonValue;
                JsonValue host = JsonValue::MakeObject();
                const bool open = session.project.Get() != nullptr;
                host.Set(u8"projectOpen", JsonValue::MakeBool(open));
                if (open)
                {
                    host.Set(u8"projectName",
                             JsonValue::MakeString(String(session.project->Name())));
                    host.Set(u8"projectDirectory",
                             JsonValue::MakeString(String(session.project->Directory())));
                }
                return host;
            }});

    StdioTransport transport;
    Serve(server, transport);
    return 0;
}
