// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Tools.Mcp - headless MCP stdio server.
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
    // Resolution walks up from the executable (then the cwd) checking the distribution layout first
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
                const fs::path candidates[] = {dir / "KnownIssues.md", // distribution: staged sidecar
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

    // Directory containing this executable (where Engine.Player + its runtime sidecars live -
    // the export host-template source). argv[0] can be bare/relative, so canonicalize.
    [[nodiscard]] String ToolDir(const char* argv0)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const std::string dir = fs::weakly_canonical(fs::absolute(fs::path(argv0), ec), ec)
                                    .parent_path()
                                    .string();
        return String(StringView(reinterpret_cast<const utf8char*>(dir.c_str())));
    }

    // The shipping docs directory (Documentation/Shipping in the engine checkout), resolved with
    // the same walk-up. Same distribution stance as KnownIssues.md: this is the CURATED,
    // distribution-facing docs set - internal design/spec/process docs are never exposed.
    [[nodiscard]] String FindShippingDocsDir(const char* argv0)
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
                const fs::path candidate = dir / "Documentation" / "Shipping";
                if (fs::is_directory(candidate, ec))
                {
                    const std::string text = candidate.string();
                    return String(StringView(reinterpret_cast<const utf8char*>(text.c_str())));
                }
                if (dir == dir.root_path())
                {
                    break;
                }
            }
        }
        return String();
    }

    // Register every shipping doc (*.md) as a read-only `docs://<FileName>` resource. Readers
    // re-read the file per request, so edits are live without restarting the host.
    void RegisterShippingDocResources(McpServer& server, StringView docsDir)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path root(reinterpret_cast<const char*>(String(docsDir).CStr()));
        for (const fs::directory_entry& entry : fs::directory_iterator(root, ec))
        {
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".md")
            {
                continue;
            }
            const std::string fileName = entry.path().filename().string();
            const std::string fullPath = entry.path().string();
            const String name(StringView(reinterpret_cast<const utf8char*>(fileName.c_str())));
            const String path(StringView(reinterpret_cast<const utf8char*>(fullPath.c_str())));
            server.RegisterResource(
                Format(u8"docs://{}", name.AsView()), name, String(u8"text/markdown"),
                Format(u8"engine documentation: {} (curated, distribution-facing)", name.AsView()),
                [path]() -> Result<String, String>
                {
                    FileStream stream(path.AsView(), FileMode::Read);
                    if (!stream.IsValid())
                    {
                        return Err(Format(u8"could not read '{}'", path.AsView()));
                    }
                    const i64 size = stream.Size();
                    Array<byte> bytes;
                    bytes.Resize(static_cast<usize>(size));
                    if (stream.Read(bytes.Data(), bytes.Size()) != static_cast<u64>(size))
                    {
                        return Err(Format(u8"could not read '{}'", path.AsView()));
                    }
                    return String(StringView(reinterpret_cast<const utf8char*>(bytes.Data()),
                                             bytes.Size()));
                });
        }
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
    // bound API, not just core - metadata only, no subsystem instantiated (headless). Every enabled
    // backend (angelscript | luau) was already registered by RegisterPipelineTypes above (the
    // composition root registers both, each OPTION_HAS-guarded), so script_api spans them all.
    engine::RegisterAllScriptFacades();
    // Component reflection for every engine domain (data-version gates) - scene_validate parses
    // component payloads through the full manager set (Engine.SceneSurface), which needs the
    // reflected field metadata registered before any scene stream deserializes.
    engine::RegisterAllSceneComponentReflection();

    // The host's builder + importer registries (from the pipeline composition root); populated
    // once, they outlive the server and back asset_cook / asset_import.
    pipeline::BuilderRegistry builders;
    pipeline::ImporterRegistry importers;
    pipeline::RegisterAllBuilders(builders);
    pipeline::RegisterAllImporters(importers);

    McpServer server;
    server.SetServerInfo(u8"engine-mcp", u8"0.1.0");
    RegisterReflectionTools(server);
    RegisterScriptTools(server); // script_api: the per-backend bound API for writing scripts

    // The host's current project (project_open/create populate it); outlives the server.
    editor::mcp::ProjectSession session;
    editor::mcp::RegisterProjectTools(server, session);
    editor::mcp::RegisterAssetTools(server, session);
    editor::mcp::RegisterAssetWriteTools(server, session, builders, importers);
    editor::mcp::RegisterAssetUsesTool(server, session, builders); // reverse deps (pre-delete read)
    editor::mcp::RegisterProjectHealthTool(server, session, builders); // the soundness sweep
    // Diagnostics: the captured engine log (incremental reads + agent markers) + the curated
    // known-issues register.
    editor::mcp::RegisterLogTools(server, logBuffer, FindKnownIssues(argv[0]));
    editor::mcp::RegisterSceneTools(server, session); // scene/prefab read+write+validate (files-first)
    // Read-only context by URI: the curated shipping docs (docs://<name>) + the open project's
    // scene/prefab XML sources (project://scene|prefab/<guid>, listed live).
    const String shippingDocs = FindShippingDocsDir(argv[0]);
    if (!shippingDocs.IsEmpty())
    {
        RegisterShippingDocResources(server, shippingDocs.AsView());
    }
    editor::mcp::RegisterProjectResources(server, session);
    // script_validate: compile-check-only (no typed checks); the
    // language cooks were registered by Pipeline::Registration above.
    editor::mcp::RegisterScriptValidateTool(server);
    editor::mcp::RegisterScriptCreateTool(server, session); // starter-seeded script assets
    // project_export: the ONE export entry point (identical to the editor menu + export CLI).
    editor::mcp::RegisterProjectExportTool(server, session, builders, ToolDir(argv[0]));
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
