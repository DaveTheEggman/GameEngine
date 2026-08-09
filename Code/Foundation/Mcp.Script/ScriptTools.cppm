// Foundation::Mcp.Script - `foundation.mcp.script`
//
// The scripting MCP tool contribution: script_api. Dumps the PER-BACKEND bound API - the exact
// script-visible names and signatures a given language (Wren, AngelScript, ...) actually bound -
// so an agent can write correct scripts instead of guessing. This is the reflection registry's
// answer projected through a real backend: DescribeBoundApi says what a backend BOUND and how it
// spells it (they differ per language), which is what a script author needs, not the C++ names.
//
// Backend-neutral: it resolves whatever backends the host registered (ScriptBackendRegistry) at
// call time, spins up a throwaway manager per language, replays the standard facade + reflected-
// type registration (the same sequence the editor's API browser uses), and reads the result. The
// host must have registered its backends (e.g. via Pipeline::Registration's RegisterPipelineTypes)
// before the tool is called.

module;
#include "Core/Prelude.h"

export module foundation.mcp.script;

import foundation.core;
import foundation.json;
import foundation.mcp;
import foundation.script;
import foundation.script.facades;

using namespace foundation::core;
using foundation::json::JsonValue;

namespace foundation::mcp::script_detail
{
    inline StringView KindName(foundation::script::ScriptApiMemberKind kind)
    {
        switch (kind)
        {
        case foundation::script::ScriptApiMemberKind::Property:
            return u8"property";
        case foundation::script::ScriptApiMemberKind::Constant:
            return u8"constant";
        case foundation::script::ScriptApiMemberKind::Method:
        default:
            return u8"method";
        }
    }

    // Build the {language, displayName, typeCount, types:[{scriptName,isNamespace,members:[...]}]}
    // object for one backend, spinning up a throwaway manager and reading its bound API.
    inline JsonValue DescribeBackend(const foundation::script::ScriptBackendDesc& backend)
    {
        JsonValue out = JsonValue::MakeObject();
        out.Set(u8"language", JsonValue::MakeString(backend.languageId));
        out.Set(u8"displayName", JsonValue::MakeString(backend.displayName));

        RefPtr<foundation::script::IScriptManager> manager =
            foundation::script::CreateScriptManagerForLanguage(backend.languageId.AsView());
        JsonValue types = JsonValue::MakeArray();
        if (manager.Get() != nullptr)
        {
            foundation::script::RegisterReflectedTypes(*manager);
            for (const foundation::script::ScriptApiType& t : manager->DescribeBoundApi())
            {
                JsonValue typeObj = JsonValue::MakeObject();
                typeObj.Set(u8"scriptName", JsonValue::MakeString(t.scriptName));
                typeObj.Set(u8"isNamespace", JsonValue::MakeBool(t.isNamespace));
                JsonValue members = JsonValue::MakeArray();
                for (const foundation::script::ScriptApiMember& m : t.members)
                {
                    JsonValue memberObj = JsonValue::MakeObject();
                    memberObj.Set(u8"name", JsonValue::MakeString(m.name));
                    memberObj.Set(u8"signature", JsonValue::MakeString(m.signature));
                    memberObj.Set(u8"isStatic", JsonValue::MakeBool(m.isStatic));
                    memberObj.Set(u8"kind", JsonValue::MakeString(String(KindName(m.kind))));
                    members.Add(Move(memberObj));
                }
                typeObj.Set(u8"members", Move(members));
                types.Add(Move(typeObj));
            }
        }
        out.Set(u8"typeCount", JsonValue::MakeNumber(static_cast<f64>(types.Count())));
        out.Set(u8"types", Move(types));
        return out;
    }
}

export namespace foundation::mcp
{
    // Registers script_api against `server`. The backends it reports are whatever the host has
    // registered in the global ScriptBackendRegistry (resolved live at call time).
    inline void RegisterScriptTools(McpServer& server)
    {
        server.RegisterTool(
            u8"script_api",
            u8"The per-backend bound scripting API: every script-visible type and member a language "
            u8"(Wren/AngelScript/...) actually binds, spelled the way scripts use it. Use it to write "
            u8"correct scripts. Optional 'language' narrows to one backend.",
            SchemaBuilder()
                .Str(u8"language", u8"backend language id (e.g. \"wren\", \"angelscript\"); default: all")
                .Build(),
            [](const JsonValue& args) -> Result<JsonValue, String>
            {
                const String language = args.Get(u8"language").AsString();

                // The standard headless build sequence (mirrors the editor's API browser): core
                // types + the script facades must be reflected before a manager can bind them.
                RegisterCoreTypes();
                foundation::script::RegisterScriptFacadeReflection();

                JsonValue languages = JsonValue::MakeArray();
                bool matched = false;
                for (const foundation::script::ScriptBackendDesc& backend :
                     foundation::script::ScriptBackendRegistry::Get().All())
                {
                    if (!language.IsEmpty() && backend.languageId.AsView() != language.AsView())
                    {
                        continue;
                    }
                    matched = true;
                    languages.Add(script_detail::DescribeBackend(backend));
                }

                if (!language.IsEmpty() && !matched)
                {
                    return Err(Format(u8"no scripting backend registered for language '{}'",
                                      language.AsView()));
                }

                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"languages", Move(languages));
                return out;
            });
    }
}
