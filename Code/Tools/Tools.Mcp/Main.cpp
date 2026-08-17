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
#include <filesystem>

import foundation.core;
import foundation.json;
import foundation.mcp;
import foundation.mcp.reflection;
import foundation.mcp.script;
import pipeline.core;
import pipeline.importer;
import pipeline.registration;
import engine.scriptsurface;
import engine.scenesurface;
#ifdef OPTION_HAS_LUAU
import foundation.script.luau;
#endif
import editor.core; // EditorLogBuffer (the host's log capture)
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

    // The CURATED, distribution-facing register (Documentation/Shipping/KnownIssues.md) - never
    // the repo-root development tracker, which is internal triage state and is not distributed.
    // Resolution walks up from the executable (then the cwd) checking the SHIPPED layout first
    // (KnownIssues.md staged next to the tool) and the repo layout second. "" = not found - the
    // known_issues tool then errs with guidance instead of being silently absent.
    [[nodiscard]] String FindKnownIssues(const char* argv0)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path starts[] = {fs::weakly_canonical(fs::absolute(fs::path(argv0), ec), ec)
                                       .parent_path(),
                                   fs::current_path(ec)};
        for (const fs::path& start : starts)
        {
            for (fs::path dir = start; !dir.empty(); dir = dir.parent_path())
            {
                const fs::path candidates[] = {dir / "KnownIssues.md", // shipped: staged sidecar
                                               dir / "Documentation" / "Shipping" /
                                                   "KnownIssues.md"}; // engine checkout
                for (const fs::path& candidate : candidates)
                {
                    if (fs::is_regular_file(candidate, ec))
                    {
                        const std::string text = candidate.string();
                        return String(
                            StringView(reinterpret_cast<const utf8char*>(text.c_str())));
                    }
                }
                if (dir == dir.root_path())
                {
                    break;
                }
            }
        }
        return String();
    }
}

int main(int /*argc*/, char** argv)
{
    // The log capture FIRST (before anything logs), so log_read sees the whole run; stderr
    // mirror second - stdout is the protocol stream.
    static editor::EditorLogBuffer logBuffer;
    GlobalLogger().AddSink(&logBuffer);
    static StderrSink stderrSink;
    GlobalLogger().AddSink(&stderrSink);

    // Populate the reflection registry with the surface we can introspect headlessly, plus the
    // full pipeline type set (so type_list sees every asset/product type and asset_cook can build).
    RegisterCoreTypes();
    foundation::json::RegisterJsonTypes();
    pipeline::RegisterPipelineTypes();
    // The COMPLETE engine script surface (every subsystem facade), so script_api reports the whole
    // bound API, not just core - metadata only, no subsystem instantiated (headless). Plus Luau, so
    // script_api spans wren | angelscript | luau (the pipeline types already registered wren + as).
    engine::RegisterAllScriptFacades();
    // Component reflection for every engine domain (data-version gates) - scene_validate parses
    // component payloads through the full manager set (Engine.SceneSurface), which needs the
    // reflected field metadata registered before any scene stream deserializes.
    engine::RegisterAllSceneComponentReflection();
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
    editor::mcp::RegisterAssetUsesTool(server, session, builders); // reverse deps (pre-delete read)
    editor::mcp::RegisterProjectHealthTool(server, session, builders); // the soundness sweep
    // Diagnostics: the captured engine log (incremental reads + agent markers) + KNOWN_ISSUES.md.
    editor::mcp::RegisterLogTools(server, logBuffer, FindKnownIssues(argv[0]));
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
