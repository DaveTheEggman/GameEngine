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

bool GameInstance::StartScript(core::StringView source, core::StringView name)
{
    StopScript();
    // THIS instance's run host is the game's context (game-instance.md §11.10). The host was
    // configured by the app (ConfigureRunHost) with the facades + Scene.spawn + entity.send routing.
    m_runHost.SetExternalErrorSink(m_errorHandler);   // before the context is created
    dscript::IScriptContext* context = m_runHost.EnsureContextForFile(name);
    if (context == nullptr)
    {
        DRACONIC_LOG_ERROR(u8"App", u8"no script backend for '{}'", name);
        return false;
    }
    m_scriptContext = core::RefPtr<dscript::IScriptContext>(context);
    m_runHost.SetGameScriptHold(true);

    const bool loaded = m_scriptContext->Load(source, name).IsOk();
    m_runHost.NoteExternalLoad();   // the game script loaded its own module (behaviors reload target)
    if (!loaded)
    {
        DRACONIC_LOG_ERROR(u8"App", u8"game script '{}' failed to compile", name);
        StopScript();
        return false;
    }
    m_game = m_scriptContext->CreateInstance(u8"Game", core::Span<core::Variant>{});
    if (m_game.Get() == nullptr)
    {
        DRACONIC_LOG_ERROR(u8"App", u8"game script '{}' has no `Game` class (construct new())", name);
        StopScript();
        return false;
    }
    (void)m_game->Invoke(u8"launch", core::Span<core::Variant>{});
    DRACONIC_LOG_INFO(u8"App", u8"game script '{}' launched", name);
    return true;
}

void GameInstance::StopScript()
{
    if (m_game.Get() != nullptr)
    {
        (void)m_game->Invoke(u8"exit", core::Span<core::Variant>{});
        m_game = nullptr;
    }
    m_scriptContext = nullptr;   // drop the game script's ref; the run host owns the context
    m_runHost.SetGameScriptHold(false);
    m_runHost.SetExternalErrorSink(nullptr);
    // The run host tears down when nothing else pins it - driven by the scene-stop observer
    // (ScriptSubsystem::MaybeTeardownRunHost). A bare instance (no scenes) keeps it until destruction.
}

dscene::Scene* GameInstance::CreateScene(core::StringView name)
{
    dscene::Scene* scene = m_sceneManager.CreateScene(name);
    if (scene != nullptr)
    {
        // OnSceneCreated (the ScriptSubsystem) added the ScriptSceneSystem + bound it to the DEFAULT
        // host; re-bind it to THIS instance's host so its behaviors share the game's context.
        if (auto* system = scene->GetSystem<dscript::ScriptSceneSystem>()) { system->SetRunHost(&m_runHost); }
    }
    return scene;
}

void GameInstance::DriveRunHost(f32 deltaTime)
{
    auto& binding = m_runHost.Binding();
    binding.timeSeconds += static_cast<core::f64>(deltaTime);
    binding.deltaSeconds = deltaTime;
    if (m_runHost.Manager() != nullptr) { m_runHost.Manager()->CollectGarbage(); }
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
