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

import foundation.core;
import foundation.json;
import foundation.content;
import foundation.mcp;
import editor.core;

using namespace foundation::core;
using foundation::json::JsonValue;
namespace content = foundation::content;

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

    // A Guid as its canonical 36-char string.
    inline JsonValue GuidToJson(const Guid& id)
    {
        utf8char buffer[37];
        id.ToChars(buffer);
        return JsonValue::MakeString(String(buffer));
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
    // The MCP host's current project (null until project_open succeeds). Owned by the host; must
    // outlive the server the tools are registered on.
    struct ProjectSession
    {
        UniquePtr<editor::EditorProject> project;
    };

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
}
