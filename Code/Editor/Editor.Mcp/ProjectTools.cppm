// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Mcp - `editor.mcp`
//
// The project MCP tool contribution over foundation.mcp, built on the headless EditorProject (VFS +
// manifest; no UI): project_info + the asset tools here, the scene / script / health / export /
// log tools in the partitions, and at the end RegisterEngineTools - the ONE list of the engine
// surface every host serves (the stdio host and the editor host compose through it, so they
// cannot drift; kEngineToolCount is its tripwire). project_create / project_open are the stdio
// host's own additions (RegisterProjectOpenTools): the editor's project is the editor's. A
// ProjectSession points every tool at the current project without owning it.

module;
#include "Core/Prelude.h"

export module editor.mcp;
export import :session;
export import :scene_tools;
export import :asset_uses;
export import :project_health;
export import :log_tools;
export import :resources;
export import :script_validate;
export import :script_create;
export import :project_export;

import foundation.core;
import foundation.json;
import foundation.content;
import foundation.vfs;
import foundation.mcp;
import foundation.mcp.reflection;
import foundation.mcp.script;
import pipeline.core;
import pipeline.importer;
import pipeline.cook;
import editor.core;

using namespace foundation::core;
using foundation::json::JsonValue;
namespace content = foundation::content;
namespace vfs = foundation::vfs;

namespace editor::mcp::detail
{
    // The two content databases a project exposes, as an enum choice list.
    inline Array<String> DatabaseChoices()
    {
        Array<String> choices;
        choices.PushBack(String(u8"source"));
        choices.PushBack(String(u8"cooked"));
        return choices;
    }

    // Recursively append every instance under `group` (depth-first) to `out` as {guid,name,type,
    // typeNamespace,group}. `path` is the slash-joined group path ("" at the root).
    inline void CollectAssets(content::Group* group, const String& path, JsonValue& out)
    {
        if (group == nullptr)
        {
            return;
        }
        for (content::Instance* inst : group->Instances())
        {
            JsonValue e = JsonValue::MakeObject();
            e.Set(u8"guid", GuidToJson(inst->Id()));
            e.Set(u8"name", JsonValue::MakeString(String(inst->Name())));
            e.Set(u8"type", JsonValue::MakeString(String(inst->TypeName())));
            e.Set(u8"typeNamespace", JsonValue::MakeString(String(inst->TypeNamespace())));
            if (!path.IsEmpty())
            {
                e.Set(u8"group", JsonValue::MakeString(path));
            }
            out.Add(Move(e));
        }
        for (content::Group* sub : group->Groups())
        {
            const String childPath =
                path.IsEmpty() ? String(sub->Name()) : Format(u8"{}/{}", path.AsView(), sub->Name());
            CollectAssets(sub, childPath, out);
        }
    }
}

export namespace editor::mcp
{
    // Registers project_create / project_open / project_info against `server`, backed by `session`.
    // Registers project_create / project_open - the STDIO host's additions. What project_open
    // opens is stored in `owner` and the session is pointed at it; the editor host, whose project
    // is the editor's own, registers neither.
    inline void RegisterProjectOpenTools(foundation::mcp::McpServer& server,
                                         ProjectSession& session, ProjectOwner& owner)
    {
        using foundation::mcp::SchemaBuilder;
        using foundation::mcp::ToolResult;
        ProjectSession* s = &session;
        ProjectOwner* o = &owner;

        server.RegisterTool(
            u8"project_create",
            u8"Scaffold a new project (Project.xml manifest + the standard directory layout) at a "
            u8"directory. Does not open it - call project_open next.",
            SchemaBuilder()
                .Str(u8"directory", u8"path to create the project at", true)
                .Str(u8"name", u8"the project's display name", true)
                .Build(),
            [](const JsonValue& args) -> ToolResult
            {
                const String directory = args.Get(u8"directory").AsString();
                const String name = args.Get(u8"name").AsString();
                const Status st = editor::EditorProject::Create(editor::EditorRootAllocator(), directory.AsView(), name.AsView());
                if (!st.IsOk())
                {
                    return Err(Format(u8"could not create project at '{}' (error {})",
                                      directory.AsView(), static_cast<i32>(st.Code())));
                }
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"created", JsonValue::MakeBool(true));
                out.Set(u8"directory", JsonValue::MakeString(directory));
                return out;
            });

        server.RegisterTool(
            u8"project_open",
            u8"Open a project (mounts its source + cooked content databases) as the session's "
            u8"current project.",
            SchemaBuilder().Str(u8"directory", u8"the project directory", true).Build(),
            [s, o](const JsonValue& args) -> ToolResult
            {
                const String directory = args.Get(u8"directory").AsString();
                UniquePtr<editor::EditorProject> opened =
                    editor::EditorProject::Open(editor::EditorRootAllocator(), directory.AsView());
                if (!opened)
                {
                    return Err(Format(u8"could not open project at '{}' (missing or unreadable "
                                      u8"Project.xml?)",
                                      directory.AsView()));
                }
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"name", JsonValue::MakeString(String(opened->Name())));
                out.Set(u8"directory", JsonValue::MakeString(String(opened->Directory())));
                s->project = nullptr; // the previous project dies with its owner slot
                o->project = Move(opened);
                s->project = o->project.Get();
                return out;
            });
    }

    // Registers project_info against `server` - the open project's identity, part of the
    // surface every host serves.
    inline void RegisterProjectInfoTool(foundation::mcp::McpServer& server,
                                        ProjectSession& session)
    {
        using foundation::mcp::SchemaBuilder;
        using foundation::mcp::ToolResult;
        ProjectSession* s = &session;

        server.RegisterTool(
            u8"project_info",
            u8"Details about the currently-open project (name, directory, sources root).",
            SchemaBuilder().Build(),
            [s](const JsonValue& /*args*/) -> ToolResult
            {
                if (!s->project)
                {
                    return Err(String(u8"no project is open (call project_open first)"));
                }
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"name", JsonValue::MakeString(String(s->project->Name())));
                out.Set(u8"directory", JsonValue::MakeString(String(s->project->Directory())));
                out.Set(u8"sourcesRoot", JsonValue::MakeString(s->project->SourcesRoot()));
                return out;
            });
    }

    // Registers asset_list / asset_info against `server`, reading the open project's content
    // databases. Register alongside the project tools (same ProjectSession).
    inline void RegisterAssetTools(foundation::mcp::McpServer& server, ProjectSession& session)
    {
        using foundation::mcp::SchemaBuilder;
        using foundation::mcp::ToolResult;
        ProjectSession* s = &session;

        const auto pickDb = [](ProjectSession* sess, const JsonValue& args) -> content::ContentDatabase&
        {
            return args.Get(u8"database").AsString() == StringView(u8"cooked")
                       ? sess->project->CookedDb()
                       : sess->project->SourceDb();
        };

        server.RegisterTool(
            u8"asset_list",
            u8"List the assets in the open project's content database (guid, name, type, group).",
            SchemaBuilder()
                .Enum(u8"database", detail::DatabaseChoices(),
                      u8"which content database (default: source)")
                .Build(),
            [s, pickDb](const JsonValue& args) -> ToolResult
            {
                if (!s->project)
                {
                    return Err(String(u8"no project is open (call project_open first)"));
                }
                JsonValue assets = JsonValue::MakeArray();
                detail::CollectAssets(pickDb(s, args).RootGroup(), String(), assets);
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"count", JsonValue::MakeNumber(static_cast<f64>(assets.Count())));
                out.Set(u8"assets", Move(assets));
                return out;
            });

        server.RegisterTool(
            u8"asset_info",
            u8"Details about one asset in the open project, by guid.",
            SchemaBuilder()
                .Str(u8"guid", u8"the asset guid (canonical 8-4-4-4-12 form)", true)
                .Enum(u8"database", detail::DatabaseChoices(),
                      u8"which content database (default: source)")
                .Build(),
            [s, pickDb](const JsonValue& args) -> ToolResult
            {
                if (!s->project)
                {
                    return Err(String(u8"no project is open (call project_open first)"));
                }
                const String guidText = args.Get(u8"guid").AsString();
                Guid id;
                if (!Guid::TryParse(guidText.AsView(), id))
                {
                    return Err(Format(u8"invalid guid '{}'", guidText.AsView()));
                }
                content::Instance* inst = pickDb(s, args).GetInstance(id);
                if (inst == nullptr)
                {
                    return Err(Format(u8"no asset with guid '{}'", guidText.AsView()));
                }
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"guid", detail::GuidToJson(inst->Id()));
                out.Set(u8"name", JsonValue::MakeString(String(inst->Name())));
                out.Set(u8"type", JsonValue::MakeString(String(inst->TypeName())));
                out.Set(u8"typeNamespace", JsonValue::MakeString(String(inst->TypeNamespace())));
                return out;
            });
    }

    // Registers asset_import / asset_cook - the WRITE side. Both are HEADLESS (editor closed):
    // import routes an OS file through the shared importer set into the open project's source DB;
    // cook runs the incremental cook driver over the project. `builders` and `importers` are the
    // host's registries (populated once from Pipeline::Registration) and must outlive the server.
    inline void RegisterAssetWriteTools(foundation::mcp::McpServer& server, ProjectSession& session,
                                        pipeline::BuilderRegistry& builders,
                                        pipeline::ImporterRegistry& importers)
    {
        using foundation::mcp::SchemaBuilder;
        using foundation::mcp::ToolResult;
        ProjectSession* s = &session;
        pipeline::ImporterRegistry* imp = &importers;
        pipeline::BuilderRegistry* bld = &builders;

        server.RegisterTool(
            u8"asset_import",
            u8"Import an OS file into the open project: copy it under Sources/ and create the typed "
            u8"Asset in the source database (routed by extension). Does not cook - call asset_cook "
            u8"next.",
            SchemaBuilder()
                .Str(u8"source", u8"absolute path to the file to import", true)
                .Str(u8"group", u8"source-DB group path to place it in (slash-joined; default root)")
                .Build(),
            [s, imp](const JsonValue& args) -> ToolResult
            {
                if (!s->project)
                {
                    return Err(String(u8"no project is open (call project_open first)"));
                }
                const String source = args.Get(u8"source").AsString();
                const String ext = pipeline::FileExtensionLower(source.AsView());
                pipeline::IFileImporter* importer = imp->FindFor(ext.AsView());
                if (importer == nullptr)
                {
                    return Err(Format(u8"no importer registered for '.{}' files", ext.AsView()));
                }
                content::Group* group = detail::ResolveGroupPath(
                    s->project->SourceDb().RootGroup(), args.Get(u8"group").AsString().AsView());

                pipeline::ImportContext ctx{editor::EditorRootAllocator(),
                                            s->project->SourcesRoot().AsView()};
                // The editor's two-phase path, run inline: the worker prepare (the load), the
                // main-thread fan-out, then the deferred flush - timed apart, so the tool
                // reports what the editor's UI thread would have paid (`mainMs`).
                const Stopwatch prepareClock = Stopwatch::StartNew();
                RefPtr<Object> prepared =
                    importer->WantsWorkerPrepare()
                        ? importer->PrepareOnWorker(source.AsView(), editor::EditorRootAllocator())
                        : RefPtr<Object>{};
                const i64 prepareMs = static_cast<i64>(prepareClock.Elapsed().AsMilliseconds());
                Array<pipeline::DeferredImportWrite> deferred;
                const Stopwatch mainClock = Stopwatch::StartNew();
                Result<content::Instance*> imported =
                    importer->Import(source.AsView(), ctx, *group, nullptr, prepared.Get(),
                                     &deferred);
                const i64 mainMs = static_cast<i64>(mainClock.Elapsed().AsMilliseconds());
                const Stopwatch flushClock = Stopwatch::StartNew();
                if (imported.HasValue())
                {
                    for (pipeline::DeferredImportWrite& write : deferred)
                    {
                        const Status written = write.Execute();
                        if (!written.IsOk())
                        {
                            return Err(Format(u8"import of '{}': deferred write '{}' failed",
                                              source.AsView(), write.Label()));
                        }
                    }
                }
                const i64 flushMs = static_cast<i64>(flushClock.Elapsed().AsMilliseconds());
                if (!imported.HasValue())
                {
                    return Err(Format(u8"import of '{}' failed (error {})", source.AsView(),
                                      static_cast<i32>(imported.Error())));
                }
                content::Instance* inst = imported.Value();
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"prepareMs", JsonValue::MakeNumber(static_cast<f64>(prepareMs)));
                out.Set(u8"mainMs", JsonValue::MakeNumber(static_cast<f64>(mainMs)));
                out.Set(u8"flushMs", JsonValue::MakeNumber(static_cast<f64>(flushMs)));
                out.Set(u8"deferredWrites", JsonValue::MakeNumber(static_cast<f64>(deferred.Size())));
                out.Set(u8"guid", detail::GuidToJson(inst->Id()));
                out.Set(u8"name", JsonValue::MakeString(String(inst->Name())));
                out.Set(u8"type", JsonValue::MakeString(String(inst->TypeName())));
                out.Set(u8"typeNamespace", JsonValue::MakeString(String(inst->TypeNamespace())));
                out.Set(u8"importer", JsonValue::MakeString(String(importer->Label())));
                return out;
            });

        server.RegisterTool(
            u8"asset_cook",
            u8"Run the incremental cook over the open project: plan the dirty set and build it into "
            u8"the cooked database. Returns the cook stats (planned/cooked/failed/orphans).",
            SchemaBuilder()
                .Boolean(u8"force", u8"re-cook every buildable asset regardless of cleanliness")
                .Build(),
            [s, bld](const JsonValue& args) -> ToolResult
            {
                if (!s->project)
                {
                    return Err(String(u8"no project is open (call project_open first)"));
                }
                const bool force = args.Get(u8"force").AsBool();
                // Second mounts on Sources/ + Cache/ (the cook driver hashes source files and
                // persists the pipeline DB through these); the source/cooked DBs are already open.
                const String sourcesRoot = s->project->SourcesRoot();
                const String cacheRoot = s->project->CacheRoot();
                vfs::NativeFileSystem sourcesMount(sourcesRoot.AsView(), editor::EditorRootAllocator());
                vfs::NativeFileSystem cacheMount(cacheRoot.AsView(), editor::EditorRootAllocator());
                // The editor's cook runs its items and their inner work over a job system;
                // the tool does the same so its timings mean what the editor's would.
                JobSystem jobs(editor::EditorRootAllocator());
                pipeline::CookDriver driver(editor::EditorRootAllocator(),
                                            s->project->SourceDb(), s->project->CookedDb(), *bld,
                                            &sourcesMount, &cacheMount, &jobs);
                pipeline::CookPlan plan = driver.Plan(force);
                const pipeline::CookStats stats = driver.Execute(plan);

                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"planned", JsonValue::MakeNumber(static_cast<f64>(plan.dirty.Size())));
                out.Set(u8"cooked", JsonValue::MakeNumber(static_cast<f64>(stats.cooked)));
                out.Set(u8"failed", JsonValue::MakeNumber(static_cast<f64>(stats.failed)));
                out.Set(u8"orphansSwept",
                        JsonValue::MakeNumber(static_cast<f64>(stats.orphansSwept)));
                out.Set(u8"upToDate", JsonValue::MakeNumber(static_cast<f64>(plan.upToDate)));
                out.Set(u8"unbuildable", JsonValue::MakeNumber(static_cast<f64>(plan.unbuildable)));
                return out;
            });
    }
}

export namespace editor::mcp
{
    // Paths the shared surface needs that only a host can discover, each host its own way (the
    // stdio host walks up from its executable, the editor knows its data root). An empty
    // knownIssues path leaves known_issues erring with guidance; an empty shippingDocsDir
    // registers no docs:// resources.
    struct EngineToolPaths
    {
        String knownIssues;     ///< the curated KnownIssues.md (Documentation/Shipping)
        String shippingDocsDir; ///< the curated shipping docs directory (docs://<name>)
        String hostToolDir;     ///< where Engine.Player + its runtime sidecars live (project_export)
        String dataRoot;        ///< the engine data root (the shader cook reads <dataRoot>/Shaders)
    };

    // The number of tools RegisterEngineTools registers. A new engine tool bumps this
    // DELIBERATELY; a lost registration then fails the test loudly (the Pipeline::Registration
    // pattern). host_info and the stdio host's project_create / project_open are NOT in it -
    // each host registers its own.
    inline constexpr usize kEngineToolCount = 21;

    // Every *.md in `docsDir` as a read-only `docs://<FileName>` resource: the CURATED,
    // distribution-facing docs set (internal design/spec/process docs are never exposed).
    // Readers re-read the file per request, so edits are live without restarting the host.
    inline void RegisterShippingDocResources(foundation::mcp::McpServer& server,
                                             StringView docsDir)
    {
        Array<String> names;
        (void)ListDirectory(
            docsDir,
            [](void* ctx, StringView name, bool isDirectory)
            {
                if (!isDirectory && name.EndsWith(u8".md"))
                {
                    static_cast<Array<String>*>(ctx)->PushBack(String(name));
                }
            },
            &names);
        for (const String& name : names)
        {
            const String path = PathJoin(docsDir, name.AsView());
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

    // The engine tool surface EVERY MCP host serves, listed ONCE: reflection (type_list /
    // type_info), script_api, project_info, the asset tools (list / info / import / cook / uses),
    // project_health, the log tools (log_read / log_write / known_issues), the scene and prefab
    // tools, script_validate / script_create, project_export, and the docs:// + project://
    // resources. A host adds what only it can serve on top (the stdio host: project_create /
    // project_open; the editor: its live tools) and its own host_info.
    inline void RegisterEngineTools(foundation::mcp::McpServer& server, ProjectSession& session,
                                    pipeline::BuilderRegistry& builders,
                                    pipeline::ImporterRegistry& importers,
                                    editor::EditorLogBuffer& logBuffer,
                                    const EngineToolPaths& paths)
    {
        foundation::mcp::RegisterReflectionTools(server);
        foundation::mcp::RegisterScriptTools(server);
        RegisterProjectInfoTool(server, session);
        RegisterAssetTools(server, session);
        RegisterAssetWriteTools(server, session, builders, importers);
        RegisterAssetUsesTool(server, session, builders);
        RegisterProjectHealthTool(server, session, builders);
        RegisterLogTools(server, logBuffer, paths.knownIssues);
        RegisterSceneTools(server, session);
        if (!paths.shippingDocsDir.IsEmpty())
        {
            RegisterShippingDocResources(server, paths.shippingDocsDir.AsView());
        }
        RegisterProjectResources(server, session);
        RegisterScriptValidateTool(server);
        RegisterScriptCreateTool(server, session);
        RegisterProjectExportTool(server, session, builders, paths.hostToolDir, paths.dataRoot);
    }
}

export namespace editor::mcp
{
    // Locates the curated shipping docs for a host's EngineToolPaths, walking UP from each
    // start directory in turn (a host passes its executable's directory, then its working
    // directory) and checking the distribution layout first (KnownIssues.md staged beside the
    // executable) and the engine checkout second (Documentation/Shipping/). The first hit
    // wins per field; a field left empty means not found - known_issues then errs with
    // guidance and no docs:// resources register. Same walk for both hosts, so they agree.
    inline void LocateShippingDocs(Span<const String> starts, EngineToolPaths& paths)
    {
        for (const String& start : starts)
        {
            for (StringView dir = start.AsView(); !dir.IsEmpty(); dir = PathParent(dir))
            {
                if (paths.knownIssues.IsEmpty())
                {
                    const String staged = PathJoin(dir, u8"KnownIssues.md");
                    if (FileExists(staged.AsView()))
                    {
                        paths.knownIssues = staged;
                    }
                }
                const String shipping = PathJoin(PathJoin(dir, u8"Documentation"), u8"Shipping");
                if (DirectoryExists(shipping.AsView()))
                {
                    if (paths.shippingDocsDir.IsEmpty())
                    {
                        paths.shippingDocsDir = shipping;
                    }
                    if (paths.knownIssues.IsEmpty())
                    {
                        const String checkout = PathJoin(shipping.AsView(), u8"KnownIssues.md");
                        if (FileExists(checkout.AsView()))
                        {
                            paths.knownIssues = checkout;
                        }
                    }
                }
                if (!paths.knownIssues.IsEmpty() && !paths.shippingDocsDir.IsEmpty())
                {
                    return;
                }
            }
        }
    }
}
