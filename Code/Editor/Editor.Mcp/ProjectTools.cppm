// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Mcp - `editor.mcp`
//
// The project MCP tool contribution: project_create / project_open / project_info. Lets an agent
// scaffold and open a project HEADLESSLY (editor closed) and inspect it, over foundation.mcp. Built
// on the headless EditorProject (VFS + manifest; no UI). A ProjectSession holds the host's currently
// open project - the tools read/mutate it. Asset/scene/pipeline tools join this lib (and Pipeline's)
// as they land.

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
    inline void RegisterProjectTools(foundation::mcp::McpServer& server, ProjectSession& session)
    {
        using foundation::mcp::SchemaBuilder;
        using foundation::mcp::ToolResult;
        ProjectSession* s = &session;

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
                const Status st = editor::EditorProject::Create(directory.AsView(), name.AsView());
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
            [s](const JsonValue& args) -> ToolResult
            {
                const String directory = args.Get(u8"directory").AsString();
                UniquePtr<editor::EditorProject> opened =
                    editor::EditorProject::Open(directory.AsView());
                if (!opened)
                {
                    return Err(Format(u8"could not open project at '{}' (missing or unreadable "
                                      u8"Project.xml?)",
                                      directory.AsView()));
                }
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"name", JsonValue::MakeString(String(opened->Name())));
                out.Set(u8"directory", JsonValue::MakeString(String(opened->Directory())));
                s->project = Move(opened);
                return out;
            });

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

                pipeline::ImportContext ctx;
                ctx.sourcesRoot = s->project->SourcesRoot();
                Result<content::Instance*> imported =
                    importer->Import(source.AsView(), ctx, *group);
                if (!imported.HasValue())
                {
                    return Err(Format(u8"import of '{}' failed (error {})", source.AsView(),
                                      static_cast<i32>(imported.Error())));
                }
                content::Instance* inst = imported.Value();
                JsonValue out = JsonValue::MakeObject();
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
                vfs::NativeFileSystem sourcesMount(sourcesRoot.AsView());
                vfs::NativeFileSystem cacheMount(cacheRoot.AsView());
                pipeline::CookDriver driver(s->project->SourceDb(), s->project->CookedDb(), *bld,
                                            &sourcesMount, &cacheMount, nullptr); // serial (no jobs)
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
