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
import foundation.mcp;
import editor.core;

using namespace foundation::core;
using foundation::json::JsonValue;

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
}
