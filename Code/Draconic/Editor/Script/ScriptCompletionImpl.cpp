// Draconic::EditorScript - the `draconic.editor.script` module.
//
// ScriptApiCompletionProvider implementation: builds the bound-API surface once (throwaway
// manager for the page's language, fed the same curated type set the runtime registers) and
// serves candidates - type/namespace names at top level, a type's members after `Type.`.

module;
#include "Core/Prelude.h"

module draconic.editor.script;

import draconic.core;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.script;
import draconic.script.facades;

using namespace draconic::core;

namespace draconic::editor
{
    namespace toolkit = draconic::ui::toolkit;
    namespace script = draconic::script;

    void ScriptApiCompletionProvider::EnsureSurface()
    {
        if (m_built || m_language.IsEmpty())
        {
            return;
        }
        m_built = true; // one attempt; a language without a backend just stays empty

        // The runtime's exact registration sequence (ScriptSubsystem), against a throwaway
        // manager: idempotent registries + a manager that dies right after DescribeBoundApi.
        core::RegisterCoreTypes();
        script::RegisterScriptFacadeReflection();
        RefPtr<script::IScriptManager> manager =
            script::CreateScriptManagerForLanguage(m_language.AsView());
        if (manager.Get() == nullptr)
        {
            return;
        }
        script::RegisterReflectedTypes(*manager);
        m_surface = manager->DescribeBoundApi();
    }

    void ScriptApiCompletionProvider::Collect(const toolkit::CodeDocument& document,
                                              toolkit::CodePosition cursor, StringView prefix,
                                              Array<toolkit::CompletionCandidate>& out)
    {
        EnsureSurface();
        if (m_surface.IsEmpty())
        {
            return;
        }

        // Member context: the prefix sits immediately right of a '.', and the word before
        // that dot names a bound type/namespace.
        const i32 anchorColumn = cursor.column - static_cast<i32>(Utf8Length(prefix));
        if (anchorColumn >= 1 &&
            document.CodepointAt(toolkit::CodePosition{cursor.line, anchorColumn - 1}) == u8'.')
        {
            if (anchorColumn < 2)
            {
                return;
            }
            const toolkit::CodeSpan owner =
                document.WordAt(toolkit::CodePosition{cursor.line, anchorColumn - 2});
            if (owner.IsEmpty())
            {
                return;
            }
            const String ownerName = document.TextInSpan(owner);
            for (const script::ScriptApiType& type : m_surface)
            {
                if (type.scriptName.AsView() != ownerName.AsView())
                {
                    continue;
                }
                for (const script::ScriptApiMember& member : type.members)
                {
                    out.PushBack(toolkit::CompletionCandidate{String(member.name.AsView()),
                                                              String(member.name.AsView())});
                }
                return;
            }
            return; // unknown receiver: offer nothing (the word provider still contributes)
        }

        // Top level: the bound type/namespace names.
        for (const script::ScriptApiType& type : m_surface)
        {
            out.PushBack(toolkit::CompletionCandidate{String(type.scriptName.AsView()),
                                                      String(type.scriptName.AsView())});
        }
    }
}
