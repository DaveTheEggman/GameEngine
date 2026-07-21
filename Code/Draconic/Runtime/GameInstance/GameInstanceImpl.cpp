// Draconic::RuntimeGameInstance - the script-bracket bodies (moved verbatim from DefaultApplication's
// former StartGameScript/StopGameScript/TickGameScript, now per-instance).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module draconic.runtime.gameinstance;

import draconic.core;
import draconic.scene;
import draconic.script;
import draconic.script.subsystem;

using namespace draconic::core;

namespace draconic::runtime {

bool GameInstance::StartScript(dscript::ScriptSubsystem* scripts,
                              const core::Function<void(dscript::IScriptContext&)>& exposeServices,
                              core::StringView source, core::StringView name)
{
    StopScript(scripts);
    if (scripts != nullptr)
    {
        // The SHARED run context (scripting.md): the game script + entity behaviors live in the ONE
        // gameplay context the subsystem owns; Stop releases the hold and the subsystem tears down.
        scripts->SetExternalErrorSink(m_errorHandler);
        dscript::IScriptContext* shared = scripts->AcquireRunContextForFile(name);
        if (shared == nullptr)
        {
            DRACONIC_LOG_ERROR(u8"App", u8"no script backend for '{}'", name);
            return false;
        }
        m_scriptContext = core::RefPtr<dscript::IScriptContext>(shared);
    }
    else
    {
        // Headless / no-subsystem fallback: self-owned manager + context. Backends + script APIs are
        // registered by the app's Configure; the caller's exposeServices binds the per-context services.
        m_scriptManager = dscript::CreateScriptManagerForFile(name);
        if (m_scriptManager.Get() == nullptr)
        {
            DRACONIC_LOG_ERROR(u8"App", u8"no script backend for '{}'", name);
            return false;
        }
        dscript::RegisterReflectedTypes(*m_scriptManager);
        m_scriptContext = m_scriptManager->CreateContext();
        if (m_errorHandler != nullptr) { m_scriptContext->SetErrorHandler(m_errorHandler); }
        if (exposeServices) { exposeServices(*m_scriptContext); }
    }

    const bool loaded = m_scriptContext->Load(source, name).IsOk();
    if (scripts != nullptr) { scripts->NoteExternalLoad(); }
    if (!loaded)
    {
        DRACONIC_LOG_ERROR(u8"App", u8"game script '{}' failed to compile", name);
        StopScript(scripts);
        return false;
    }
    m_game = m_scriptContext->CreateInstance(u8"Game", core::Span<core::Variant>{});
    if (m_game.Get() == nullptr)
    {
        DRACONIC_LOG_ERROR(u8"App", u8"game script '{}' has no `Game` class (construct new())", name);
        StopScript(scripts);
        return false;
    }
    (void)m_game->Invoke(u8"launch", core::Span<core::Variant>{});
    DRACONIC_LOG_INFO(u8"App", u8"game script '{}' launched", name);
    return true;
}

void GameInstance::StopScript(dscript::ScriptSubsystem* scripts)
{
    if (m_game.Get() != nullptr)
    {
        (void)m_game->Invoke(u8"exit", core::Span<core::Variant>{});
        m_game = nullptr;
    }
    if (scripts != nullptr)
    {
        // Shared context: the SUBSYSTEM owns handler + lifetime; just release this run's hold.
        m_scriptContext = nullptr;
        m_scriptManager = nullptr;
        scripts->ReleaseRunContext();
        return;
    }
    if (m_scriptContext.Get() != nullptr) { m_scriptContext->SetErrorHandler(nullptr); }
    m_scriptContext = nullptr;
    m_scriptManager = nullptr;
}

void GameInstance::TickScript(f32 hostDeltaTime, f32 contextTimeScale)
{
    if (m_game.Get() == nullptr) { return; }
    const f32 sceneScale = m_scene != nullptr ? m_scene->TimeScale() : 1.0f;
    core::Variant dt = core::Variant::From(hostDeltaTime * contextTimeScale * m_instanceTimeScale * sceneScale);
    if (auto result = m_game->Invoke(u8"update", core::Span<core::Variant>{ &dt, 1 }); !result.HasValue())
    {
        DRACONIC_LOG_ERROR(u8"App", u8"game script update() faulted - stopping script");
        m_game = nullptr;
    }
}

}
