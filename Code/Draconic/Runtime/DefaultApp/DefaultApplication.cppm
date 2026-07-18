// Draconic::RuntimeDefaultApp - the `draconic.runtime.defaultapp` module.
//
// DefaultApplication: an opinionated IApplication base that registers the standard
// engine subsystems. A game that wants the batteries-included engine writes
// `class MyGame : DefaultApplication` and adds its own subsystems in Configure
// (calling the base first); a game that wants only its own subsystems implements
// IApplication directly and links none of this.
//
// This lives in its OWN library - separate from draconic.runtime.client - precisely
// so the base client never pulls in the engine subsystem libraries. It registers ALL
// standard gameplay subsystems (runtime-host.md v3: the editor embeds THIS same class
// against its runtime context, so subsystem registration lives here, not in entry
// points) and owns the GAME-SCRIPT lifecycle (the Wren `Game` class bracket) - the
// player and the editor's Game tab both consume it instead of hand-rolling copies.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.runtime.defaultapp;

import draconic.core;
import draconic.rhi;
import draconic.runtime.client;     // IApplication, IApplicationHost
import draconic.shell;   // IShell, IKeyboard, KeyCode (the profile-dump hotkey)
import draconic.graphics;   // GraphicsDevice, FrameContext
import draconic.scene;              // Scene
import draconic.scene.subsystem;    // SceneSubsystem (the standard scene driver)
import draconic.render.subsystem;   // RenderSubsystem (the standard renderer)
import draconic.animation.subsystem; // AnimationSubsystem (drives skeletal animation from the scene)
import draconic.particles.subsystem; // ParticleSubsystem (scene-driven CPU sim)
import draconic.physics.subsystem;  // PhysicsSubsystem (Jolt worlds + interpolation)
import draconic.input;              // the action model/runtime
import draconic.input.subsystem;    // InputSubsystem + the Wren Input facade
import draconic.script;             // IScriptManager/Context (the game script)
import draconic.script.wren;        // the Wren backend
import draconic.profiler;           // the CPU scope profiler (P-key dump)

namespace rhi = draconic::rhi;
namespace core  = draconic::core;
using namespace draconic::shell;   // IShell + input/window types (moved from draconic::runtime)
using namespace draconic::graphics;   // GraphicsDevice/RenderWindow/FrameContext (moved from draconic::runtime)

export namespace draconic::runtime
{
    class DefaultApplication : public IApplication
    {
    public:
        // Press P to print the previous frame's CPU scope tree + per-pass GPU timing. A game
        // subclass that overrides OnUpdate should call DefaultApplication::OnUpdate(host, dt) to
        // keep the hotkey. (Reads the GPU timestamps after a device stall - fine for an on-demand dump.)
        void OnUpdate(IApplicationHost& host, core::f32 deltaTime) override
        {
            TickGameScript(host, deltaTime);
            IShell* plat = host.Shell();
            IInputManager* input = (plat != nullptr) ? plat->Input() : nullptr;
            IKeyboard* kb = (input != nullptr) ? input->Keyboard() : nullptr;
            if (kb == nullptr || !kb->IsKeyPressed(KeyCode::P)) { return; }

            core::ConsoleWrite(draconic::profiler::Profiler::Get().BuildReport().AsView());
            if (auto* renderer = host.Ctx().GetSubsystem<draconic::render::RenderSubsystem>())
            {
                core::String gpu;
                renderer->BuildGpuProfileReport(gpu);
                core::ConsoleWrite(gpu.AsView());
            }
        }

        // Ticks the game script with GAMEPLAY time: dt x context scale x the primary
        // scene's scale (per-scene time, H1). Subclasses overriding OnUpdate call the
        // base to keep the script (and the profile hotkey) alive.
        void TickGameScript(IApplicationHost& host, core::f32 deltaTime)
        {
            if (m_game.Get() == nullptr) { return; }
            const core::f32 sceneScale = m_primaryScene != nullptr ? m_primaryScene->TimeScale() : 1.0f;
            core::Variant dt = core::Variant::From(deltaTime * host.Ctx().TimeScale() * sceneScale);
            if (auto result = m_game->Invoke(u8"update", core::Span<core::Variant>{ &dt, 1 });
                !result.HasValue())
            {
                DRACONIC_LOG_ERROR(u8"App", u8"game script update() faulted - stopping script");
                m_game = nullptr;
            }
        }

        // Registers ALL standard engine subsystems. A game subclass overrides this,
        // calls DefaultApplication::Configure(host) first, then adds its own. Entry
        // points (player, editor) do NOT register gameplay subsystems - this is the
        // one place (runtime-host.md v3).
        void Configure(IApplicationHost& host) override
        {
            host.Ctx().AddSubsystem<draconic::scene::SceneSubsystem>();
            if (GraphicsDevice* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr)
            {
                host.Ctx().AddSubsystem<draconic::render::RenderSubsystem>(*gfx->Raw(), gfx->FramesInFlight());
                // Drives skeletal animation from the scene tick (injects the SkeletalAnimation manager,
                // ticks players, feeds bone matrices to mesh components). Needs the render managers.
                host.Ctx().AddSubsystem<draconic::animation::AnimationSubsystem>();
                host.Ctx().AddSubsystem<draconic::particles::ParticleSubsystem>();
            }
            m_physics = host.Ctx().AddSubsystem<draconic::physics::PhysicsSubsystem>();
            m_input = host.Ctx().AddSubsystem<draconic::input::InputSubsystem>(
                host.Shell() != nullptr ? host.Shell()->Input() : nullptr);
        }

        [[nodiscard]] draconic::input::InputSubsystem* Input() const noexcept { return m_input; }
        [[nodiscard]] draconic::physics::PhysicsSubsystem* Physics() const noexcept { return m_physics; }

        // ---- the game script (a Wren class `Game`: construct new(), launch(), update(dt),
        // exit() - all optional except the class). Faults disable the SCRIPT, not the game. ----

        /// The scene whose time scale the script's update(dt) follows (and, later, the
        /// scene game services bind against). Set by the launch flow; null = context time.
        void SetPrimaryScene(draconic::scene::Scene* scene) noexcept { m_primaryScene = scene; }
        [[nodiscard]] draconic::scene::Scene* PrimaryScene() const noexcept { return m_primaryScene; }

        /// Optional per-run error sink (the editor surfaces notices); set BEFORE
        /// StartGameScript, cleared automatically on StopGameScript.
        void SetGameScriptErrorHandler(draconic::script::IScriptErrorHandler* handler) noexcept
        {
            m_scriptErrorHandler = handler;
        }

        /// Compiles + launches the game script from source text. The CALLER resolves
        /// where the source lives (player: project file / pak entry; editor: SourceDb).
        /// Registers the script facades and exposes the per-context services.
        bool StartGameScript(core::StringView source, core::StringView name)
        {
            StopGameScript();
            draconic::input::RegisterInputScriptApi();
            draconic::physics::RegisterPhysicsScriptApi();
            m_scriptManager = draconic::script::wren::CreateScriptManager();
            draconic::script::RegisterReflectedTypes(*m_scriptManager);
            m_scriptContext = m_scriptManager->CreateContext();
            if (m_scriptErrorHandler != nullptr)
            {
                m_scriptContext->SetErrorHandler(m_scriptErrorHandler);
            }
            if (m_input != nullptr) { m_input->ExposeToScript(*m_scriptContext); }
            if (m_physics != nullptr) { m_physics->ExposeToScript(*m_scriptContext); }
            if (!m_scriptContext->Load(source, name).IsOk())
            {
                DRACONIC_LOG_ERROR(u8"App", u8"game script '{}' failed to compile", name);
                StopGameScript();
                return false;
            }
            m_game = m_scriptContext->CreateInstance(u8"Game", core::Span<core::Variant>{});
            if (m_game.Get() == nullptr)
            {
                DRACONIC_LOG_ERROR(u8"App", u8"game script '{}' has no `Game` class (construct new())", name);
                StopGameScript();
                return false;
            }
            (void)m_game->Invoke(u8"launch", core::Span<core::Variant>{});
            DRACONIC_LOG_INFO(u8"App", u8"game script '{}' launched", name);
            return true;
        }

        /// exit() + teardown (idempotent; the update fault path also lands here).
        void StopGameScript()
        {
            if (m_game.Get() != nullptr)
            {
                (void)m_game->Invoke(u8"exit", core::Span<core::Variant>{});
                m_game = nullptr;
            }
            if (m_scriptContext.Get() != nullptr) { m_scriptContext->SetErrorHandler(nullptr); }
            m_scriptContext = nullptr;
            m_scriptManager = nullptr;
        }
        [[nodiscard]] bool GameScriptRunning() const noexcept { return m_game.Get() != nullptr; }

        // Default render: draw every active scene into the window via the RenderSubsystem.
        // A game overrides this for custom rendering. (Single-scene for now - multiple
        // active scenes would each clear; compositing is a later concern.)
        void OnRenderWindow(IApplicationHost& host, FrameContext& frame) override
        {
            auto* render = host.Ctx().GetSubsystem<draconic::render::RenderSubsystem>();
            auto* scenes = host.Ctx().GetSubsystem<draconic::scene::SceneSubsystem>();
            if (render == nullptr || !render->IsReady() || scenes == nullptr ||
                frame.encoder == nullptr || frame.backbufferView == nullptr || frame.window == nullptr)
            {
                frame.Clear(0.08f, 0.09f, 0.12f, 1.0f);   // no renderer - present a clear
                return;
            }

            const rhi::TextureFormat colorFormat = frame.window->Swap()->Format();
            render->BeginRendering(*frame.encoder, frame.frameIndex);
            for (draconic::scene::Scene* scene : scenes->ActiveScenes())
            {
                render->RenderScene(*scene, frame.backbufferView, colorFormat,
                                    frame.width, frame.height);   // clear comes from the scene's camera
            }
            render->EndRendering();
        }

    private:
        draconic::input::InputSubsystem* m_input = nullptr;
        draconic::physics::PhysicsSubsystem* m_physics = nullptr;
        draconic::scene::Scene* m_primaryScene = nullptr;
        draconic::script::IScriptErrorHandler* m_scriptErrorHandler = nullptr;
        core::RefPtr<draconic::script::IScriptManager> m_scriptManager;
        core::RefPtr<draconic::script::IScriptContext> m_scriptContext;
        core::RefPtr<draconic::script::ScriptObject> m_game;
    };
}
